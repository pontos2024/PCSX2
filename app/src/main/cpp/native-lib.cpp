#include <jni.h>
#include <android/native_window_jni.h>
#include "PrecompiledHeader.h"
#include "common/StringUtil.h"
#include "common/FileSystem.h"
#include "common/pxStreams.h"
#include "pcsx2/GS.h"
#include "pcsx2/VMManager.h"
#include "pcsx2/HostDisplay.h"
#include "PAD/Host/PAD.h"
#include "PAD/Host/KeyStatus.h"
#include "PerformanceMetrics.h"
#include "Frontend/GameList.h"
#include "Frontend/ImGuiManager.h"
#include "GS/Renderers/SW/GSScanlineEnvironment.h"
#include "Frontend/InputManager.h"
#include "common/Timer.h"
#include "common/Vulkan/Context.h"
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <future>


bool s_execute_exit;
int s_window_width = 0;
int s_window_height = 0;
ANativeWindow* s_window = nullptr;

WindowInfo g_gs_window_info;
static std::unique_ptr<HostDisplay> s_host_display;
////
__aligned16 static SysMtgsThread s_mtgs_thread;
SysMtgsThread& GetMTGS() {
    return s_mtgs_thread;
}
////
const IConsoleWriter* PatchesCon = &Console;
void LoadAllPatchesAndStuff(const Pcsx2Config& cfg) {
}
////
std::string GetJavaString(JNIEnv *env, jstring jstr) {
    if (!jstr) {
        return "";
    }
    const char *str = env->GetStringUTFChars(jstr, nullptr);
    std::string cpp_string = std::string(str);
    env->ReleaseStringUTFChars(jstr, str);
    return cpp_string;
}


extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_initialize(JNIEnv *env, jclass clazz,
                                                jstring p_szpath, jint p_apiVer) {
    ANDROID_API_VERSION = p_apiVer;
    std::string _szPath = GetJavaString(env, p_szpath);
    ////
    EmuFolders::AppRoot = wxDirName(_szPath);
    EmuFolders::DataRoot = wxDirName(_szPath);
    EmuFolders::SetDefaults();
    EmuFolders::EnsureFoldersExist();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getGameTitle(JNIEnv *env, jclass clazz,
                                                 jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    GameList::Entry entry;
    GameList::PopulateEntryFromPath(_szPath, &entry);

    std::string ret;
    ret.append(entry.title);
    ret.append("|");
    ret.append(entry.serial);
    ret.append("|");
    ret.append(StringUtil::StdStringFromFormat("%s (%08X)", entry.serial.c_str(), entry.crc));

    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = VMManager::GetGameSerial();
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jfloat JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getFPS(JNIEnv *env, jclass clazz) {
    return (jfloat)PerformanceMetrics::GetFPS();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getPauseGameTitle(JNIEnv *env, jclass clazz) {
    std::string ret = VMManager::GetGameName();
    return env->NewStringUTF(ret.c_str());
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getPauseGameSerial(JNIEnv *env, jclass clazz) {
    std::string ret = StringUtil::StdStringFromFormat("%s (%08X)", VMManager::GetGameSerial().c_str(), VMManager::GetGameCRC());
    return env->NewStringUTF(ret.c_str());
}


extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_setPadVibration(JNIEnv *env, jclass clazz,
                                                     jboolean p_isOnOff) {
    PAD::SetVibration(p_isOnOff);
}

extern "C" JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_setPadButton(JNIEnv *env, jclass clazz,
                                                  jint p_key, jint p_range, jboolean p_keyPressed) {
    HostKeyEvent _keyEvent{};
    if(p_keyPressed) {
        _keyEvent.type = HostKeyEvent::Type::KeyPressed;
    } else {
        _keyEvent.type = HostKeyEvent::Type::KeyReleased;
    }
    _keyEvent.key = p_key;
    _keyEvent.range = p_range;
    PAD::HandleHostInputEvent(_keyEvent);
}

extern "C" JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_resetKeyStatus(JNIEnv *env, jclass clazz) {
    g_key_status.Init();
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_setAspectRatio(JNIEnv *env, jclass clazz,
                                                    jint p_type) {
    AspectRatioType _AspectRatio = static_cast<AspectRatioType>(p_type);
    if(_AspectRatio != EmuConfig2.GS.AspectRatio) {
        EmuConfig2.CurrentAspectRatio = EmuConfig2.GS.AspectRatio = _AspectRatio;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_speedhackLimitermode(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
    VMManager::SetLimiterMode(static_cast<LimiterModeType>(p_value));
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_speedhackEecyclerate(JNIEnv *env, jclass clazz,
                                                          jint p_value) {
    if(p_value != (int)EmuConfig.Speedhacks.EECycleRate) {
        EmuConfig.Speedhacks.EECycleRate = p_value;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_speedhackEecycleskip(JNIEnv *env, jclass clazz,
                                                            jint p_value) {
    if(p_value != (int)EmuConfig.Speedhacks.EECycleSkip) {
        EmuConfig.Speedhacks.EECycleSkip = p_value;
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderUpscalemultiplier(JNIEnv *env, jclass clazz,
                                                             jfloat p_value) {
    if(p_value != EmuConfig2.GS.UpscaleMultiplier) {
        EmuConfig2.GS.UpscaleMultiplier = p_value;
        ////
        if (VMManager::HasValidVM()) {
            GetMTGS().ApplySettings();
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderMipmap(JNIEnv *env, jclass clazz,
                                                  jint p_value) {
    HWMipmapLevel _hWMipmapLevel = static_cast<HWMipmapLevel>(p_value);
    if(_hWMipmapLevel != EmuConfig2.GS.HWMipmap) {
        EmuConfig2.GS.HWMipmap = _hWMipmapLevel;
        ////
        if (VMManager::HasValidVM()) {
            GetMTGS().ApplySettings();
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderHalfpixeloffset(JNIEnv *env, jclass clazz,
                                                           jint p_value) {
    int _HalfPixelOffset = p_value;
    if(_HalfPixelOffset != EmuConfig2.GS.UserHacks_HalfPixelOffset) {
        EmuConfig2.GS.UserHacks_HalfPixelOffset = _HalfPixelOffset;
        ////
        if (VMManager::HasValidVM()) {
            GetMTGS().ApplySettings();
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderPreloading(JNIEnv *env, jclass clazz,
                                                      jint p_value) {
    TexturePreloadingLevel _TexturePreloadingLevel = static_cast<TexturePreloadingLevel>(p_value);
    if(_TexturePreloadingLevel != EmuConfig2.GS.TexturePreloading) {
        EmuConfig2.GS.TexturePreloading = _TexturePreloadingLevel;
        ////
        if (VMManager::HasValidVM()) {
            GetMTGS().ApplySettings();
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_renderGpu(JNIEnv *env, jclass clazz,
                                               jint p_value) {
    GSRendererType _GSRendererType = static_cast<GSRendererType>(p_value);
    if (VMManager::HasValidVM()) {
        GetMTGS().SwitchRenderer(_GSRendererType);
    } else {
        EmuConfig2.GS.Renderer = _GSRendererType;
    }
}


extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_onNativeSurfaceCreated(JNIEnv *env, jclass clazz) {
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_onNativeSurfaceChanged(JNIEnv *env, jclass clazz,
                                                            jobject p_surface, jint p_width, jint p_height) {
    if(s_window) {
        ANativeWindow_release(s_window);
        s_window = nullptr;
    }

    if(p_surface != nullptr) {
        s_window = ANativeWindow_fromSurface(env, p_surface);
    }

    if(p_width > 0 && p_height > 0) {
        s_window_width = p_width;
        s_window_height = p_height;

        if(s_host_display != nullptr) {
            if(s_host_display->HasRenderDevice()) {
                GetMTGS().UpdateDisplayWindow();
            }
        }
    }
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_onNativeSurfaceDestroyed(JNIEnv *env, jclass clazz) {
    if(s_window) {
        ANativeWindow_release(s_window);
        s_window = nullptr;
    }
}


HostDisplay* Host::AcquireHostDisplay(HostDisplay::RenderAPI api) {
    float _fScale = 1.0;
    if (s_window_width > 0 && s_window_height > 0) {
        int _nSize = s_window_width;
        if (s_window_width <= s_window_height) {
            _nSize = s_window_height;
        }
        _fScale = (float)_nSize / 1280.0f;
    }
    ////
    memset(&g_gs_window_info, 0, sizeof(g_gs_window_info));
    g_gs_window_info.type = WindowInfo::Type::Android;
    g_gs_window_info.surface_width = s_window_width;
    g_gs_window_info.surface_height = s_window_height;
    g_gs_window_info.surface_scale = _fScale;
    g_gs_window_info.window_handle = s_window;

    // can't go anywhere if we don't have a window to render into!
    if (g_gs_window_info.type == WindowInfo::Type::Surfaceless) {
        return nullptr;
    }

    s_host_display = HostDisplay::CreateDisplayForAPI(api);
    if (!s_host_display) {
        return nullptr;
    }

    if (!s_host_display->CreateRenderDevice(g_gs_window_info, "myps2", EmuConfig2.GetEffectiveVsyncMode(),  true, false)) {
        ReleaseHostDisplay();
        return nullptr;
    }

    if (!s_host_display->MakeRenderContextCurrent())
    {
        ReleaseHostDisplay();
        return nullptr;
    }

    if (!s_host_display->InitializeRenderDevice(
            StringUtil::wxStringToUTF8String(EmuFolders::Cache.ToString()), false)) {
        ReleaseHostDisplay();
        return nullptr;
    }

    s_host_display->SetDisplayAlignment(HostDisplay::Alignment::Center);

#ifdef OSD_SHOW
    ImGuiManager::Initialize();
#endif
    return s_host_display.get();
}

void Host::ReleaseHostDisplay(bool p_isFailed) {
#ifdef OSD_SHOW
    ImGuiManager::Shutdown();
#endif
    s_host_display.reset();
}

HostDisplay* Host::GetHostDisplay() {
    return s_host_display.get();
}

bool Host::BeginPresentFrame(bool frame_skip) {
    if (s_host_display) {
        if (!s_host_display->BeginPresent(frame_skip)) {
            // if we're skipping a frame, we need to reset imgui's state, since
            // we won't be calling EndPresentFrame().
#ifdef OSD_SHOW
            ImGuiManager::NewFrame();
#endif
            return false;
        }
        return true;
    }
    return false;
}

void Host::EndPresentFrame() {
    if (s_host_display) {
#ifdef OSD_SHOW
        ImGuiManager::RenderOSD();
#endif
        s_host_display->EndPresent();
#ifdef OSD_SHOW
        ImGuiManager::NewFrame();
#endif
    }
}

void Host::ResizeHostDisplay(u32 new_window_width, u32 new_window_height, float new_window_scale) {
    if(s_host_display) {
        s_host_display->ResizeRenderWindow(new_window_width, new_window_height, new_window_scale);
#ifdef OSD_SHOW
        ImGuiManager::WindowResized();
#endif
    }
}


std::optional<std::vector<u8>> Host::ReadResourceFile(const char* filename) {
    const std::string path(Path::Combine(EmuFolders::Resources.ToString(), filename));
    std::optional<std::vector<u8>> ret(FileSystem::ReadBinaryFile(path.c_str()));
    if (!ret.has_value()) {
        Console.Error("Failed to read resource file '%s'", filename);
    }
    return ret;
}

std::optional<std::string> Host::ReadResourceFileToString(const char* filename) {
    const std::string path(Path::Combine(EmuFolders::Resources.ToString(), filename));
    std::optional<std::string> ret(FileSystem::ReadFileToString(path.c_str()));
    if (!ret.has_value()) {
        Console.Error("Failed to read resource file to string '%s'", filename);
    }
    return ret;
}

void Host::GameChanged(const std::string &disc_path, const std::string &game_serial, const std::string &game_name, u32 game_crc) {
}

void Host::PumpMessagesOnCPUThread() {
}

void Host::InvalidateSaveStateCache() {
}

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view& str)
{
    return std::nullopt;
}


extern "C"
JNIEXPORT jboolean JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_runVMThread(JNIEnv *env, jclass clazz,
                                                 jstring p_szpath) {
    std::string _szPath = GetJavaString(env, p_szpath);

    s_execute_exit = false;
    PerformanceMetrics::SetCPUThread(Threading::ThreadHandle::GetForCallingThread());

    // fast_boot : (false:bios->game, true:game)
    VMBootParameters boot_params;
    VMManager::SetBootParametersForPath(_szPath, &boot_params);

    if(VMManager::InitializeMemory())
    {
        if (VMManager::Initialize(boot_params))
        {
            VMState _vmState = VMState::Running;
            VMManager::SetState(_vmState);
            ////
            while (true)
            {
                _vmState = VMManager::GetState();
                if (_vmState == VMState::Stopping || _vmState == VMState::Shutdown)
                {
                    break;
                }
                else if (_vmState == VMState::Running)
                {
                    s_execute_exit = false;
                    VMManager::Execute();
                    s_execute_exit = true;
                }
                else
                {
                    usleep(250000);
                }
            }
            ////
            VMManager::Shutdown(false);
        }
        ////
        VMManager::ReleaseMemory();
    }

    PerformanceMetrics::SetCPUThread(Threading::ThreadHandle());

    return true;
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_pause(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetPaused(true);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_resume(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetPaused(false);
    }).detach();
}

extern "C"
JNIEXPORT void JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_shutdown(JNIEnv *env, jclass clazz) {
    std::thread([] {
        VMManager::SetState(VMState::Stopping);
    }).detach();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_saveStateToSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
        u32 _crc = VMManager::GetGameCRC();
        if(_crc != 0) {
            if(VMManager::GetState() != VMState::Paused) {
                VMManager::SetPaused(true);
            }

            // wait 5 sec
            for (int i = 0; i < 5; ++i) {
                if (s_execute_exit) {
                    if(VMManager::SaveStateToSlot(p_slot)) {
                        return true;
                    }
                    break;
                }
                sleep(1);
            }
        }
        return false;

    });

    return ret.get();
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_loadStateFromSlot(JNIEnv *env, jclass clazz, jint p_slot) {
    if (!VMManager::HasValidVM()) {
        return false;
    }

    std::future<bool> ret = std::async([p_slot]
    {
        u32 _crc = VMManager::GetGameCRC();
        if(_crc != 0) {
            std::string _serial = VMManager::GetGameSerial();
            if (VMManager::HasSaveStateInSlot(_serial.c_str(), _crc, p_slot)) {
                if(VMManager::GetState() != VMState::Paused) {
                    VMManager::SetPaused(true);
                }

                // wait 5 sec
                for (int i = 0; i < 5; ++i) {
                    if (s_execute_exit) {
                        if(VMManager::LoadStateFromSlot(p_slot)) {
                            return true;
                        }
                        break;
                    }
                    sleep(1);
                }
            }
        }
        return false;
    });

    return ret.get();
}

extern "C"
JNIEXPORT jstring JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getGamePathSlot(JNIEnv *env, jclass clazz, jint slot) {
    u32 _crc = VMManager::GetGameCRC();
    std::string _serial = VMManager::GetGameSerial();
    std::string _filename = VMManager::GetSaveStateFileNameSerial(_serial.c_str(), _crc, slot);
    if(!_filename.empty()) {
        if(!FileSystem::FileExists(_filename.c_str())) {
            _filename = VMManager::GetSaveStateFileName(_serial.c_str(), _crc, slot);
        }
        if(!_filename.empty()) {
            return env->NewStringUTF(_filename.c_str());
        }
    }
    return nullptr;
}

extern "C"
JNIEXPORT jbyteArray JNICALL
Java_kr_co_iefriends_pcsx2_NativeApp_getImageSlot(JNIEnv *env, jclass clazz, jint slot) {
    jbyteArray retArr = nullptr;

    u32 _crc = VMManager::GetGameCRC();
    std::string _serial = VMManager::GetGameSerial();
    std::string _filename = VMManager::GetSaveStateFileNameSerial(_serial.c_str(), _crc, slot);
    if(!_filename.empty()) {
        if(!FileSystem::FileExists(_filename.c_str())) {
            _filename = VMManager::GetSaveStateFileName(_serial.c_str(), _crc, slot);
        }
        if(!_filename.empty()) {
            std::unique_ptr<wxFFileInputStream> woot(new wxFFileInputStream(_filename));
            if (woot->IsOk()) {
                std::unique_ptr<pxInputStream> reader(
                        new pxInputStream(_filename, new wxZipInputStream(woot.get())));
                woot.release();

                if (reader->IsOk()) {
                    auto *gzreader = (wxZipInputStream *) reader->GetWxStreamBase();
                    while (true) {
                        std::unique_ptr<wxZipEntry> entry(gzreader->GetNextEntry());
                        if (!entry) {
                            break;
                        }
                        if (entry->GetName().CmpNoCase("Screenshot.png") == 0) {
                            if (gzreader->OpenEntry(*entry)) {
                                if (gzreader->CanRead()) {
                                    int _fileSize = (int) entry->GetSize();
                                    unsigned char _cBuffer[_fileSize];
                                    gzreader->Read(_cBuffer, _fileSize);
                                    ////
                                    retArr = env->NewByteArray(_fileSize);
                                    env->SetByteArrayRegion(retArr, 0, _fileSize,
                                                            (jbyte *) _cBuffer);
                                }
                            }
                            break;
                        }
                    }
                }
                reader->Close();
            }
        }
    }

    return retArr;
}
