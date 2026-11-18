/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "UdfpsHandler.xiaomi_sm8750"

#include <aidl/android/hardware/biometrics/fingerprint/AcquiredInfo.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "UdfpsHandler.h"

namespace {

// Commands of the Xiaomi fingerprint HAL modules. The touchscreen, not the
// sensor, detects the finger, so the module is told where and when it lands
// on the sensor area.
constexpr int32_t kCommandFodPressStatus = 1;
constexpr int32_t kCommandFodPressX = 2;
constexpr int32_t kCommandFodPressY = 3;

constexpr int32_t kParamFodReleased = 0;
constexpr int32_t kParamFodPressed = 1;

// The device is sold with either a Qualcomm (QFS4008) or a Goodix ultrasonic
// sensor, each driven by its own HAL module.
constexpr const char* kGoodixModuleName = "Goodix Fingerprint HAL";
constexpr const char* kQcomModuleName = "Qcom Fingerprint HAL";

// The Goodix HAL module reports a capture that failed on a corrupted SPI
// transfer with this vendor acquired code while the finger is still down, and
// does not retry it. Ask for another capture of the same press, a bounded
// number of times.
constexpr int32_t kGoodixVendorCodeSpiCrcFailed = 41;
constexpr int kGoodixCaptureRetries = 2;
constexpr auto kGoodixCaptureRetryDelay = std::chrono::milliseconds(50);
// A capture takes longer than this, so a failure reported sooner after a press
// belongs to the previous one.
constexpr auto kGoodixMinCaptureTime = std::chrono::milliseconds(50);

// Lets the fingerprint driver and the display know a finger is on the sensor.
constexpr const char* kFingerDownPath = "/sys/bus/platform/devices/soc:xiaomi_fingerprint/fingerdown";
// GPIO from the SoC to the sensor. The Qualcomm sensor firmware only reports a
// finger as present while it is high: the kernel drives it from touch events
// while the sensor waits for a finger, but the HAL module hands it over to
// userspace for enrollment and authentication. On the Goodix sensor the same
// GPIO is the reset line, so it must not be touched there.
constexpr const char* kIntr2Path = "/sys/bus/platform/devices/soc:xiaomi_fingerprint/intr2";

// Input device of the touchscreen, which reports every touch, including those
// on the sensor area.
constexpr const char* kInputDir = "/dev/input";
constexpr const char* kTouchscreenName = "Xiaomi_Touch_Input_0";

void writeNode(const char* path, bool value) {
    if (!android::base::WriteStringToFile(value ? "1" : "0", path)) {
        PLOG(ERROR) << "Failed to write " << path;
    }
}

std::string findTouchscreen() {
    std::unique_ptr<DIR, decltype(&closedir)> dir(opendir(kInputDir), closedir);
    if (!dir) {
        PLOG(ERROR) << "Failed to open " << kInputDir;
        return {};
    }

    while (struct dirent* entry = readdir(dir.get())) {
        if (strncmp(entry->d_name, "event", strlen("event")) != 0) {
            continue;
        }
        std::string path = std::string(kInputDir) + "/" + entry->d_name;
        android::base::unique_fd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC));
        char name[64] = {};
        if (fd >= 0 && ioctl(fd.get(), EVIOCGNAME(sizeof(name) - 1), name) >= 0 &&
            strcmp(name, kTouchscreenName) == 0) {
            return path;
        }
    }

    LOG(ERROR) << "Touchscreen " << kTouchscreenName << " not found";
    return {};
}

// Whether any finger is on the touchscreen. BTN_TOUCH is only released when the
// last one lifts. Without the state, assume a finger is on it and wait for the
// release event: a release with the finger on the sensor would spoil the
// reference image.
bool isTouching(int fd) {
    uint8_t keys[KEY_MAX / 8 + 1] = {};
    if (ioctl(fd, EVIOCGKEY(sizeof(keys)), keys) < 0) {
        PLOG(ERROR) << "Failed to read the touchscreen state";
        return true;
    }
    return keys[BTN_TOUCH / 8] & (1 << (BTN_TOUCH % 8));
}

using ::aidl::android::hardware::biometrics::fingerprint::AcquiredInfo;

}  // namespace

class XiaomiSM8750UdfpsHandler : public UdfpsHandler {
  public:
    void init(fingerprint_device_t* device) override {
        mDevice = device;
        if (!mDevice) {
            return;
        }

        const char* moduleName = mDevice->common.module->name;
        mDriveIntr2 = moduleName && strcmp(moduleName, kQcomModuleName) == 0;
        mRetryCrcFailures = moduleName && strcmp(moduleName, kGoodixModuleName) == 0;
        LOG(INFO) << "HAL module: " << (moduleName ? moduleName : "unknown");

        mWakeFd.reset(eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
        if (mWakeFd < 0) {
            PLOG(ERROR) << "Failed to create the release watcher eventfd";
            return;
        }
        std::thread(&XiaomiSM8750UdfpsHandler::watchReleases, this).detach();
    }

    void onFingerDown(uint32_t x, uint32_t y, float /*minor*/, float /*major*/) override {
        if (!mDevice) {
            return;
        }

        LOG(DEBUG) << __func__ << " x: " << x << ", y: " << y;
        std::lock_guard lock(mCommandMutex);
        mFingerDown = true;
        mPressId++;
        mPressTime = std::chrono::steady_clock::now();
        mCaptureRetries = 0;
        setFingerDown(true);
        mDevice->extCmd(mDevice, kCommandFodPressX, x);
        mDevice->extCmd(mDevice, kCommandFodPressY, y);
        mDevice->extCmd(mDevice, kCommandFodPressStatus, kParamFodPressed);
        pressChanged();
    }

    void onFingerUp() override {
        if (!mDevice) {
            return;
        }

        LOG(DEBUG) << __func__;
        std::lock_guard lock(mCommandMutex);
        release();
    }

    // Runs in the HAL module's notify callback. The retry waits for the sensor to
    // recover from the failed transfer, so it is sent from its own thread.
    void onAcquired(int32_t result, int32_t vendorCode) override {
        const uint32_t pressId = mPressId;
        if (!mRetryCrcFailures || result != static_cast<int32_t>(AcquiredInfo::VENDOR) ||
            vendorCode != kGoodixVendorCodeSpiCrcFailed || !mFingerDown ||
            std::chrono::steady_clock::now() - mPressTime.load() < kGoodixMinCaptureTime ||
            mCaptureRetries.fetch_add(1) >= kGoodixCaptureRetries) {
            return;
        }

        LOG(INFO) << "Capture failed on a SPI CRC error, retrying";
        std::thread([this, pressId] {
            std::this_thread::sleep_for(kGoodixCaptureRetryDelay);
            std::lock_guard lock(mCommandMutex);
            if (mFingerDown && mPressId == pressId) {
                mDevice->extCmd(mDevice, kCommandFodPressStatus, kParamFodPressed);
            }
        }).detach();
    }

  private:
    void setFingerDown(bool down) {
        if (mDriveIntr2) {
            writeNode(kIntr2Path, down);
        }
        writeNode(kFingerDownPath, down);
    }

    // The module takes its reference image of the empty sensor when the finger
    // is released, and can't detect the next finger until it has one, so a
    // press must end with a real release. Called with mCommandMutex held.
    void release() {
        if (!mFingerDown) {
            return;
        }

        mFingerDown = false;
        mPressId++;
        setFingerDown(false);
        mDevice->extCmd(mDevice, kCommandFodPressX, 0);
        mDevice->extCmd(mDevice, kCommandFodPressY, 0);
        mDevice->extCmd(mDevice, kCommandFodPressStatus, kParamFodReleased);
        pressChanged();
    }

    // Wakes the release watcher. Called with mCommandMutex held.
    void pressChanged() {
        mPressChanged.notify_all();
        const uint64_t wake = 1;
        if (mWakeFd >= 0 && write(mWakeFd.get(), &wake, sizeof(wake)) < 0) {
            PLOG(ERROR) << "Failed to wake the release watcher";
        }
    }

    // The framework only forwards the release of a finger while it authenticates
    // or enrolls, and drops it when the finger is still down as the operation
    // ends, such as after a successful unlock. Watch the touchscreen during every
    // press, and end the press when the last finger leaves it, if the framework
    // hasn't already.
    void watchReleases() {
        for (;;) {
            uint32_t pressId;
            {
                std::unique_lock lock(mCommandMutex);
                mPressChanged.wait(lock, [this] { return mFingerDown.load(); });
                pressId = mPressId;
            }

            if (mTouchscreenPath.empty()) {
                mTouchscreenPath = findTouchscreen();
            }
            android::base::unique_fd fd(
                    open(mTouchscreenPath.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK));
            if (fd < 0) {
                PLOG(ERROR) << "Failed to open " << mTouchscreenPath;
                mTouchscreenPath.clear();
                waitForPressEnd(pressId);
                continue;
            }

            if (waitForLift(fd.get(), pressId)) {
                std::lock_guard lock(mCommandMutex);
                if (mFingerDown && mPressId == pressId) {
                    LOG(DEBUG) << "Finger lifted from the touchscreen";
                    release();
                }
            }
        }
    }

    // Returns true when the touchscreen reports no finger, false when the press
    // ends first.
    bool waitForLift(int fd, uint32_t pressId) {
        bool touching = isTouching(fd);
        bool resync = false;
        while (touching) {
            struct pollfd fds[] = {{.fd = fd, .events = POLLIN},
                                   {.fd = mWakeFd.get(), .events = POLLIN}};
            if (poll(fds, 2, -1) < 0) {
                if (errno == EINTR) {
                    continue;
                }
                PLOG(ERROR) << "Failed to poll the touchscreen";
                waitForPressEnd(pressId);
                return false;
            }

            if (fds[1].revents & POLLIN) {
                uint64_t wake;
                if (read(mWakeFd.get(), &wake, sizeof(wake)) < 0) {
                    PLOG(ERROR) << "Failed to read the release watcher eventfd";
                }
            }
            if (!mFingerDown || mPressId != pressId) {
                return false;
            }
            if (fds[0].revents & (POLLERR | POLLHUP)) {
                LOG(ERROR) << "Touchscreen input device gone";
                mTouchscreenPath.clear();
                waitForPressEnd(pressId);
                return false;
            }

            struct input_event events[64];
            ssize_t size;
            while ((size = read(fd, events, sizeof(events))) > 0) {
                for (size_t i = 0; i < size / sizeof(events[0]); i++) {
                    const auto& event = events[i];
                    if (event.type == EV_KEY && event.code == BTN_TOUCH) {
                        touching = event.value != 0;
                    } else if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                        resync = true;
                    } else if (event.type == EV_SYN && event.code == SYN_REPORT && resync) {
                        // Events were lost: read the state instead.
                        resync = false;
                        touching = isTouching(fd);
                    }
                }
            }
        }
        return true;
    }

    void waitForPressEnd(uint32_t pressId) {
        std::unique_lock lock(mCommandMutex);
        mPressChanged.wait(lock, [this, pressId] { return !mFingerDown || mPressId != pressId; });
    }

    fingerprint_device_t* mDevice = nullptr;
    bool mDriveIntr2 = false;
    bool mRetryCrcFailures = false;

    // Serializes the commands of a press with those of capture retries and of
    // the release watcher, which are sent from their own threads.
    std::mutex mCommandMutex;
    std::condition_variable mPressChanged;
    std::atomic<bool> mFingerDown = false;
    std::atomic<uint32_t> mPressId = 0;
    std::atomic<std::chrono::steady_clock::time_point> mPressTime;
    std::atomic<int> mCaptureRetries = 0;

    // Release watcher, woken through mWakeFd when a press ends.
    android::base::unique_fd mWakeFd;
    std::string mTouchscreenPath;
};

static UdfpsHandler* create() {
    return new XiaomiSM8750UdfpsHandler();
}

static void destroy(UdfpsHandler* handler) {
    delete handler;
}

extern "C" UdfpsHandlerFactory UDFPS_HANDLER_FACTORY = {
        .create = create,
        .destroy = destroy,
};
