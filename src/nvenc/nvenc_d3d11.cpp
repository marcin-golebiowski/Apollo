/**
 * @file src/nvenc/nvenc_d3d11.cpp
 * @brief Definitions for abstract Direct3D11 NVENC encoder.
 */
// local includes
#include "src/logging.h"

#ifdef _WIN32
  #include "nvenc_d3d11.h"

namespace nvenc {

  nvenc_d3d11::nvenc_d3d11(NV_ENC_DEVICE_TYPE device_type):
      nvenc_base(device_type) {
    async_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
    flush_event_handle = CreateEvent(NULL, FALSE, FALSE, NULL);
  }

  nvenc_d3d11::~nvenc_d3d11() {
    if (dll) {
      FreeLibrary(dll);
      dll = NULL;
    }
    if (async_event_handle) {
      CloseHandle(async_event_handle);
    }
    if (flush_event_handle) {
      CloseHandle(flush_event_handle);
    }
  }

  bool nvenc_d3d11::init_library() {
    if (dll) {
      return true;
    }

  #ifdef _WIN64
    constexpr auto dll_name = "nvEncodeAPI64.dll";
  #else
    constexpr auto dll_name = "nvEncodeAPI.dll";
  #endif

    if ((dll = LoadLibraryEx(dll_name, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32))) {
      if (auto create_instance = (decltype(NvEncodeAPICreateInstance) *) GetProcAddress(dll, "NvEncodeAPICreateInstance")) {
        auto new_nvenc = std::make_unique<NV_ENCODE_API_FUNCTION_LIST>();
        new_nvenc->version = min_struct_version(NV_ENCODE_API_FUNCTION_LIST_VER);
        if (nvenc_failed(create_instance(new_nvenc.get()))) {
          BOOST_LOG(error) << "NvEnc: NvEncodeAPICreateInstance() failed: " << last_nvenc_error_string;
        } else {
          nvenc = std::move(new_nvenc);
          return true;
        }
      } else {
        BOOST_LOG(error) << "NvEnc: No NvEncodeAPICreateInstance() in " << dll_name;
      }
    } else {
      BOOST_LOG(debug) << "NvEnc: Couldn't load NvEnc library " << dll_name;
    }

    if (dll) {
      FreeLibrary(dll);
      dll = NULL;
    }

    return false;
  }

  bool nvenc_d3d11::device_removed() {
    if (auto *texture = get_input_texture()) {
      ID3D11DevicePtr d3d_device;
      texture->GetDevice(&d3d_device);
      return FAILED(d3d_device->GetDeviceRemovedReason());
    }
    return false;
  }

  async_wait_result nvenc_d3d11::wait_for_async_event(uint32_t timeout_ms, void *event_handle) {
    switch (WaitForSingleObject(event_handle ? event_handle : async_event_handle, timeout_ms)) {
      case WAIT_OBJECT_0:
        return async_wait_result::signaled;
      case WAIT_TIMEOUT:
        // A reset device may never signal the last submitted picture. End the
        // grace period (or the retiring session's drain) once removal is known.
        if (device_removed()) {
          BOOST_LOG(error) << "NvEnc: D3D11 device removed while waiting for frame completion";
          return async_wait_result::failed;
        }
        return async_wait_result::timeout;
      default:
        const auto error_code = GetLastError();
        BOOST_LOG(error) << "NvEnc: completion event wait failed (Win32 error " << error_code << ")";
        return async_wait_result::failed;
    }
  }

}  // namespace nvenc
#endif
