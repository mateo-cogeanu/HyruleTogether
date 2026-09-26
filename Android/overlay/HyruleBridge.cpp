#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include <cstddef>
// The multiplayer callbacks mirror this ABI prefix in SpawningVariables.h.
static_assert(offsetof(PPCInterpreter_t, gpr) == 4);
static_assert(offsetof(PPCInterpreter_t, fpr) == 136);
static_assert(offsetof(PPCInterpreter_t, spr) == 696);
#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

namespace {
std::mutex statusMutex;
std::string status = "Not connected";
std::atomic_bool started{false};
void setStatus(const std::string& value) {
    std::lock_guard lock(statusMutex);
    status = value;
    __android_log_print(ANDROID_LOG_INFO, "HyruleTogether", "%s", value.c_str());
}
std::string fromJava(JNIEnv* env, jstring value) {
    if (!value) return {};
    const char* chars = env->GetStringUTFChars(value, nullptr);
    if (!chars) return {};
    std::string result(chars);
    env->ReleaseStringUTFChars(value, chars);
    return result;
}
bool command(int fd, const std::string& text) {
    // A socketpair keeps commands inside this process. Wait for each reply
    // before writing another command, matching the desktop IPC protocol.
    size_t offset = 0;
    while (offset < text.size() + 1) {
        auto n = send(fd, text.c_str() + offset, text.size() + 1 - offset, MSG_NOSIGNAL);
        if (n <= 0) { setStatus("Client IPC write failed. Restart the app to retry."); return false; }
        offset += static_cast<size_t>(n);
    }
    std::string reply;
    char byte;
    while (reply.size() < 2048 && recv(fd, &byte, 1, 0) == 1) {
        if (!byte) {
            if (reply == "Succeeded") return true;
            setStatus(reply);
            return false;
        }
        reply += byte;
    }
    setStatus("Client handshake timed out or closed. Restart the app to retry.");
    return false;
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_info_cemu_cemu_HyruleSession_status(JNIEnv* env, jclass) {
    std::lock_guard lock(statusMutex);
    return env->NewStringUTF(status.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_HyruleSession_start(JNIEnv* env, jclass, jstring data,
        jstring host, jint port, jstring player, jstring password) {
    const auto directory = fromJava(env, data);
    const auto hostname = fromJava(env, host);
    const auto name = fromJava(env, player);
    const auto secret = fromJava(env, password);
    auto safe = [](const std::string& s) { return !s.starts_with("[") && s.find_first_of(";\r\n") == std::string::npos; };
    if (directory.empty() || hostname.empty() || name.empty() || port < 1 || port > 65535 ||
        !safe(hostname) || !safe(name) || !safe(secret) || name.size() > 32 ||
        hostname.size() > 253 || secret.size() > 128) {
        setStatus("Invalid server or player settings");
        return JNI_FALSE;
    }
    if (started.exchange(true)) return JNI_TRUE;
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, pair)) {
        started = false; setStatus("Could not create client IPC"); return JNI_FALSE;
    }
    timeval timeout{150, 0};
    setsockopt(pair[0], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setenv("MILKBAR_DATA_DIR", directory.c_str(), 1);
    setenv("MILKBAR_WAIT_FOR_HOOKS", "1", 1);
    setenv("HYRULE_IPC_FD", std::to_string(pair[1]).c_str(), 1);
    // Keep this handle alive until Android exits the emulation process.
    void* client = dlopen("libMilkBarClient.so", RTLD_NOW | RTLD_LOCAL);
    if (!client) {
        setStatus(std::string("Could not load multiplayer client: ") + dlerror());
        close(pair[0]); close(pair[1]); unsetenv("HYRULE_IPC_FD");
        unsetenv("MILKBAR_WAIT_FOR_HOOKS"); started = false; return JNI_FALSE;
    }
    auto startClient = reinterpret_cast<void (*)()>(dlsym(client, "hyrule_startClient"));
    if (!startClient) {
        setStatus("The packaged client is missing its Android startup entry point");
        close(pair[0]); close(pair[1]); unsetenv("HYRULE_IPC_FD");
        unsetenv("MILKBAR_WAIT_FOR_HOOKS"); started = false; return JNI_FALSE;
    }
    startClient();
    setStatus("Waiting for BOTW and emulator hooks");
    std::thread([fd = pair[0], hostname, port, name, secret] {
        const std::string connect = "!connect;" + hostname + ";" + std::to_string(port) + ";" +
            secret + ";" + name + ";Android;0;Jugador1ModelNameLongForASpecificReason:Link;";
        if (command(fd, connect) && command(fd, "!startServerLoop")) {
            setStatus("Connected to " + hostname + ":" + std::to_string(port));
            char message[2048];
            // Keep IPC alive for notifications; the native game workers own the session.
            timeval noTimeout{0, 0};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &noTimeout, sizeof(noTimeout));
            while (recv(fd, message, sizeof(message), 0) > 0) {}
        }
        close(fd);
    }).detach();
    return JNI_TRUE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_HyruleSession_isBotw(JNIEnv* env, jclass, jstring path) {
    try {
        fs::path gamePath = fromJava(env, path);
        TitleInfo title(gamePath);
        if (!title.IsValid() && gamePath.extension() == ".rpx")
            title = TitleInfo(gamePath.parent_path().parent_path());
        if (!title.IsValid()) return JNI_FALSE;
        auto id = title.GetAppTitleId();
        const bool botw = id == 0x00050000101c9300ULL || id == 0x00050000101c9400ULL || id == 0x00050000101c9500ULL;
        if (!botw) return JNI_FALSE;
        CafeTitleList::WaitForMandatoryScan();
        return title.GetAppTitleVersion() == 208 || CafeTitleList::HasTitleAndVersion(id | 0x0000000e00000000ULL, 208);
    } catch (...) { return JNI_FALSE; }
}
