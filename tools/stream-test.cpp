/**
 * @file tools/stream-test.cpp
 * @brief Fullscreen Windows test pattern captured by Apollo like any other application.
 */
#define NOMINMAX
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <mmsystem.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>

namespace {
  using Microsoft::WRL::ComPtr;
  using clock_type = std::chrono::steady_clock;
  using seconds = std::chrono::duration<double>;

  constexpr int cycle_seconds = 20;
  constexpr int cue_interval_seconds = 2;
  constexpr double cue_duration_seconds = 0.1;

  struct pattern_position {
    std::uint64_t frame;
    double phase;
    std::uint64_t cue;
    bool flash;
  };

  pattern_position position_at(double elapsed, double fps) {
    const auto frame = static_cast<std::uint64_t>(elapsed * fps);
    const double source_time = frame / fps;
    const auto cue = static_cast<std::uint64_t>(source_time / cue_interval_seconds);
    // Keep the cue visible for at least one frame even at very low rates.
    const bool flash = cue > 0 && std::fmod(source_time, cue_interval_seconds) < std::max(cue_duration_seconds, 1.0 / fps);
    return {frame, std::fmod(source_time, cycle_seconds), cue, flash};
  }

  double requested_fps() {
    wchar_t value[32] {};
    const DWORD length = GetEnvironmentVariableW(L"APOLLO_CLIENT_FPS", value, ARRAYSIZE(value));
    if (!length || length >= ARRAYSIZE(value)) {
      return 60.0;
    }
    wchar_t *end;
    const double fps = std::wcstod(value, &end);
    return (*end == L'\0' && fps >= 1 && fps <= 1000) ? fps : 60.0;
  }

  clock_type::time_point timeline_origin() {
    // The host records this epoch immediately before launch. After this one mapping,
    // wall-clock corrections cannot jump or rewind the on-screen frame number.
    const auto steady_now = clock_type::now();
    const auto wall_now = std::chrono::system_clock::now().time_since_epoch();
    wchar_t value[32] {};
    const DWORD length = GetEnvironmentVariableW(L"APOLLO_STREAM_TEST_EPOCH_MS", value, ARRAYSIZE(value));
    if (length && length < ARRAYSIZE(value)) {
      wchar_t *end;
      const auto epoch = std::wcstoll(value, &end, 10);
      const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(wall_now).count();
      if (*end == L'\0' && epoch > 0 && epoch <= now_ms && now_ms - epoch < 86400000) {
        return steady_now - std::chrono::milliseconds(now_ms - epoch);
      }
    }
    return steady_now;
  }

  struct monitor_selection {
    wchar_t name[CCHDEVICENAME] {};
    HMONITOR found {};
  };

  BOOL CALLBACK match_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto &selection = *reinterpret_cast<monitor_selection *>(data);
    MONITORINFOEXW info {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info) && _wcsicmp(info.szDevice, selection.name) == 0) {
      selection.found = monitor;
      return FALSE;
    }
    return TRUE;
  }

  RECT display_bounds() {
    monitor_selection selection;
    const DWORD length = GetEnvironmentVariableW(L"APOLLO_CAPTURE_DISPLAY", selection.name, ARRAYSIZE(selection.name));
    if (length && length < ARRAYSIZE(selection.name)) {
      EnumDisplayMonitors(nullptr, nullptr, match_monitor, reinterpret_cast<LPARAM>(&selection));
    }
    if (!selection.found) {
      selection.found = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    }
    MONITORINFO info {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(selection.found, &info);
    return info.rcMonitor;
  }

  LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
      case WM_DISPLAYCHANGE:
      case WM_DPICHANGED: {
        const RECT bounds = display_bounds();
        SetWindowPos(window, nullptr, bounds.left, bounds.top, bounds.right - bounds.left,
                     bounds.bottom - bounds.top, SWP_NOACTIVATE | SWP_NOZORDER);
        return 0;
      }
      case WM_ERASEBKGND:
        return 1;
      case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
          DestroyWindow(window);
          return 0;
        }
        break;
      case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
  }

  // Memory-backed 24 ms, 1 kHz PCM click with a short envelope to avoid pops.
  // PlaySound opens the user's current audio endpoint on each cue, including when
  // Apollo reroutes audio after the test application has already launched.
  struct click_sound {
    static constexpr std::uint32_t sample_rate = 48000;
    static constexpr std::uint32_t samples = sample_rate * 24 / 1000;
    char riff[4] {'R', 'I', 'F', 'F'};
    std::uint32_t riff_size = 36 + samples * 2;
    char wave[4] {'W', 'A', 'V', 'E'};
    char fmt[4] {'f', 'm', 't', ' '};
    std::uint32_t fmt_size = 16;
    std::uint16_t pcm = 1;
    std::uint16_t channels = 1;
    std::uint32_t rate = sample_rate;
    std::uint32_t bytes_per_sec = sample_rate * 2;
    std::uint16_t block_align = 2;
    std::uint16_t bits = 16;
    char data[4] {'d', 'a', 't', 'a'};
    std::uint32_t data_size = samples * 2;
    std::array<std::int16_t, samples> tones {};

    click_sound() {
      constexpr double tau = 6.283185307179586;
      for (std::uint32_t i = 0; i < samples; ++i) {
        const double envelope = 0.5 - 0.5 * std::cos(tau * i / (samples - 1));
        tones[i] = static_cast<std::int16_t>(8192 * envelope * std::sin(tau * 1000 * i / sample_rate));
      }
    }

    void play() const {
      PlaySoundW(reinterpret_cast<LPCWSTR>(this), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    }
  };
  static_assert(sizeof(click_sound) == 44 + click_sound::samples * 2);

  struct pattern_renderer {
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> text_factory;
    ComPtr<IDWriteTextFormat> label;
    ComPtr<IDWriteTextFormat> counter;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;

    bool init() {
      return SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf())) &&
             SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                           reinterpret_cast<IUnknown **>(text_factory.GetAddressOf()))) &&
             SUCCEEDED(text_factory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 28, L"", label.GetAddressOf())) &&
             SUCCEEDED(text_factory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 106, L"", counter.GetAddressOf()));
    }

    HRESULT prepare_target(HWND window, D2D1_SIZE_U size) {
      if (target) {
        const auto old = target->GetPixelSize();
        return old.width == size.width && old.height == size.height ? S_OK : target->Resize(size);
      }
      HRESULT hr = factory->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
          D2D1::HwndRenderTargetProperties(window, size, D2D1_PRESENT_OPTIONS_IMMEDIATELY), target.GetAddressOf());
      if (SUCCEEDED(hr)) {
        target->SetDpi(96, 96);
        hr = target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1), brush.GetAddressOf());
      }
      return hr;
    }

    void rect(float l, float t, float r, float b, UINT32 rgb) {
      brush->SetColor(D2D1::ColorF(rgb));
      target->FillRectangle(D2D1::RectF(l, t, r, b), brush.Get());
    }

    void text(const wchar_t *value, IDWriteTextFormat *font, float l, float t, float r, float b) {
      brush->SetColor(D2D1::ColorF(0xF4F6F7));
      target->DrawText(value, static_cast<UINT32>(std::wcslen(value)), font,
                       D2D1::RectF(l, t, r, b), brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    HRESULT draw(HWND window, std::uint64_t frame, double fps, double phase, bool flash) {
      RECT area {};
      GetClientRect(window, &area);
      if (!area.right || !area.bottom) {
        return S_FALSE;
      }
      HRESULT hr = prepare_target(window, D2D1::SizeU(area.right, area.bottom));
      if (SUCCEEDED(hr)) {
        const float scale = std::min(area.right / 1280.f, area.bottom / 720.f);
        const float w = area.right / scale;
        const float h = area.bottom / scale;
        target->BeginDraw();
        target->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale));
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        target->Clear(D2D1::ColorF(0x101419));
        text(L"Apollo Stream Test", label.Get(), 40, 17, w - 405, 62);
        wchar_t timing[64] {};
        std::swprintf(timing, ARRAYSIZE(timing), L"%g FPS   /   %02d of 20 s", fps, static_cast<int>(phase));
        text(timing, label.Get(), w - 400, 17, w - 12, 62);

        constexpr UINT32 bars[] = {0xBFBFBF, 0xBFBF00, 0x00BFBF, 0x00BF00, 0xBF00BF, 0xBF0000, 0x0000BF};
        const float grid_top = h * 0.41f;
        for (unsigned i = 0; i < ARRAYSIZE(bars); ++i) {
          rect(w * i / ARRAYSIZE(bars), 72, w * (i + 1) / ARRAYSIZE(bars), grid_top, bars[i]);
        }

        const float grid_bottom = h - 204;
        constexpr float cell = 56;
        // Exactly ten cells of horizontal and five cells of vertical travel per loop.
        const float dx = static_cast<float>(std::fmod(phase * cell / 2, cell));
        const float dy = static_cast<float>(std::fmod(phase * cell / 4, cell));
        rect(0, grid_top, w, grid_bottom, 0x192632);
        for (float x = dx; x < w; x += cell) {
          rect(x, grid_top, x + 2, grid_bottom, 0x7492A5);
        }
        for (float y = grid_top + dy; y < grid_bottom; y += cell) {
          rect(0, y, w, std::min(y + 2, grid_bottom), 0x7492A5);
        }

        wchar_t digits[32] {};
        std::swprintf(digits, ARRAYSIZE(digits), L"%06llu", static_cast<unsigned long long>(frame));
        text(L"Pattern frame", label.Get(), 40, grid_bottom + 14, w - 365, grid_bottom + 58);
        text(digits, counter.Get(), 36, grid_bottom + 48, w - 358, h - 4);
        rect(w - 347, grid_bottom + 22, w - 245, grid_bottom + 124, flash ? 0xFFFFFF : 0x32404C);
        text(L"Flash + click", label.Get(), w - 224, grid_bottom + 51, w - 12, grid_bottom + 100);
        text(L"Esc to exit", label.Get(), w - 224, h - 60, w - 12, h - 9);
        rect(0, h - 5, static_cast<float>(w * phase / cycle_seconds), h, 0x71CFD0);
        hr = target->EndDraw();
      }
      if (hr == D2DERR_RECREATE_TARGET) {
        brush.Reset();
        target.Reset();
      }
      return hr;
    }
  };

  void wait_for_frame(HANDLE timer, seconds remaining) {
    if (remaining.count() <= 0) {
      return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(remaining.count() * 10000000));
    if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
      MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    } else {
      const auto ms = static_cast<DWORD>(std::ceil(remaining.count() * 1000));
      MsgWaitForMultipleObjectsEx(0, nullptr, ms, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
  }
}  // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  auto set_dpi = reinterpret_cast<decltype(&SetProcessDpiAwarenessContext)>(
      GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
  if (!set_dpi || !set_dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
    SetProcessDPIAware();
  }
  pattern_renderer renderer;
  if (!renderer.init()) {
    return 1;
  }

  WNDCLASSW wc {};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = L"ApolloStreamTest";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  if (!RegisterClassW(&wc)) {
    return 1;
  }
  const RECT b = display_bounds();
  HWND window = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Apollo Stream Test", WS_POPUP,
      b.left, b.top, b.right - b.left, b.bottom - b.top, nullptr, nullptr, instance, nullptr);
  if (!window) {
    return 1;
  }
  ShowWindow(window, SW_SHOW);
  SetForegroundWindow(window);
  const auto fps = requested_fps();
  const auto origin = timeline_origin();
  const click_sound sound;
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
  if (!timer) {
    timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
  }

  std::uint64_t last_frame = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t last_cue = 0;
  int result = 0;
  while (IsWindow(window)) {
    MSG msg {};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (msg.message == WM_QUIT || !IsWindow(window)) {
      break;
    }
    const double elapsed = seconds(clock_type::now() - origin).count();
    const auto pos = position_at(elapsed, fps);
    if (pos.frame != last_frame) {
      const HRESULT hr = renderer.draw(window, pos.frame, fps, pos.phase, pos.flash);
      if (hr == S_OK && pos.flash && pos.cue != last_cue) {
        sound.play();
        last_cue = pos.cue;
      } else if (FAILED(hr) && hr != D2DERR_RECREATE_TARGET) {
        result = 1;
        break;
      }
      last_frame = pos.frame;
    }
    wait_for_frame(timer, seconds((pos.frame + 1) / fps) - seconds(clock_type::now() - origin));
  }
  PlaySoundW(nullptr, nullptr, 0);
  if (timer) {
    CloseHandle(timer);
  }
  if (IsWindow(window)) {
    DestroyWindow(window);
  }
  return result;
}
