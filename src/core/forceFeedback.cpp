#include "core/forceFeedback.h"
#include "core/joystick.h"
#include "core/log.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

namespace rally {
	namespace {
		struct Device {
			IDirectInputDevice8W* dev = nullptr;
			std::string name;
			bool ff = false;
			joy::State state;
		};

		IDirectInput8W* di = nullptr;
		std::vector<Device> devices;
		struct Found { GUID guid; std::string name; bool ff; };
		std::vector<Found> found;
		int ffIndex = -1;

		IDirectInputEffect* constantFx = nullptr;
		IDirectInputEffect* springFx = nullptr;
		IDirectInputEffect* damperFx = nullptr;
		LONG lastConstant = 0x7fffffff, lastSpring = -1, lastDamper = -1;

		// XInput, loaded at runtime so a missing DLL never stops the game.
		struct XVib { WORD left, right; };
		typedef DWORD(WINAPI* XInputSetStateFn)(DWORD, XVib*);
		XInputSetStateFn xinputSetState = nullptr;
		WORD lastLow = 0xFFFF, lastHigh = 0xFFFF;

		const joy::State kEmpty{};

		BOOL CALLBACK enumAll(LPCDIDEVICEINSTANCEW inst, LPVOID) {
			if (found.size() >= (size_t)joy::kMaxDevices) return DIENUM_STOP;
			char buf[260] = {};
			WideCharToMultiByte(CP_UTF8, 0, inst->tszProductName, -1, buf, sizeof buf, nullptr, nullptr);
			found.push_back({ inst->guidInstance, buf, false });
			return DIENUM_CONTINUE;
		}
		BOOL CALLBACK enumFF(LPCDIDEVICEINSTANCEW inst, LPVOID) {
			for (auto& f : found) if (IsEqualGUID(f.guid, inst->guidInstance)) f.ff = true;
			return DIENUM_CONTINUE;
		}

		IDirectInputDevice8W* ffDev() { return ffIndex >= 0 ? devices[ffIndex].dev : nullptr; }

		IDirectInputEffect* makeCondition(REFGUID type) {
			DWORD axes[1] = { (DWORD)DIJOFS_X };
			LONG dir[1] = { 0 };
			DICONDITION cond{};
			cond.dwPositiveSaturation = DI_FFNOMINALMAX;
			cond.dwNegativeSaturation = DI_FFNOMINALMAX;
			DIEFFECT eff{};
			eff.dwSize = sizeof(DIEFFECT);
			eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
			eff.dwDuration = INFINITE;
			eff.dwGain = DI_FFNOMINALMAX;
			eff.dwTriggerButton = DIEB_NOTRIGGER;
			eff.cAxes = 1;
			eff.rgdwAxes = axes;
			eff.rglDirection = dir;
			eff.cbTypeSpecificParams = sizeof(DICONDITION);
			eff.lpvTypeSpecificParams = &cond;
			IDirectInputEffect* fx = nullptr;
			if (FAILED(ffDev()->CreateEffect(type, &eff, &fx, nullptr))) return nullptr;
			fx->Start(1, 0);
			return fx;
		}

		void setCondition(IDirectInputEffect* fx, LONG coeff, LONG& last) {
			if (!fx || coeff == last) return;
			last = coeff;
			DICONDITION cond{};
			cond.lPositiveCoefficient = coeff;
			cond.lNegativeCoefficient = coeff;
			cond.dwPositiveSaturation = DI_FFNOMINALMAX;
			cond.dwNegativeSaturation = DI_FFNOMINALMAX;
			DIEFFECT eff{};
			eff.dwSize = sizeof(DIEFFECT);
			eff.cbTypeSpecificParams = sizeof(DICONDITION);
			eff.lpvTypeSpecificParams = &cond;
			fx->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS);
		}
	}

	// ------------------------------------------------------------------
	namespace ffb {
		bool init(void* hwndPtr) {
			HMODULE x = LoadLibraryA("xinput1_4.dll");
			if (!x) x = LoadLibraryA("xinput9_1_0.dll");
			if (x) xinputSetState = (XInputSetStateFn)(void*)GetProcAddress(x, "XInputSetState");

			HWND hwnd = (HWND)hwndPtr;
			if (FAILED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&di, nullptr))) {
				log::warn("input: DirectInput unavailable");
				return false;
			}
			di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumAll, nullptr, DIEDFL_ATTACHEDONLY);
			di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumFF, nullptr, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);

			for (const Found& f : found) {
				Device d;
				d.name = f.name;
				if (FAILED(di->CreateDevice(f.guid, &d.dev, nullptr))) continue;
				d.dev->SetDataFormat(&c_dfDIJoystick2);
				// Axis range -10000..10000 on every axis.
				DIPROPRANGE range{};
				range.diph.dwSize = sizeof(DIPROPRANGE);
				range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
				range.diph.dwHow = DIPH_DEVICE;
				range.lMin = -10000;
				range.lMax = 10000;
				d.dev->SetProperty(DIPROP_RANGE, &range.diph);
				// Force feedback needs exclusive access (first FFB device only).
				bool wantFF = f.ff && ffIndex < 0;
				if (!wantFF || FAILED(d.dev->SetCooperativeLevel(hwnd, DISCL_EXCLUSIVE | DISCL_BACKGROUND)))
					d.dev->SetCooperativeLevel(hwnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND), wantFF = false;
				d.ff = wantFF;
				if (wantFF) {
					DIPROPDWORD prop{};
					prop.diph.dwSize = sizeof(DIPROPDWORD);
					prop.diph.dwHeaderSize = sizeof(DIPROPHEADER);
					prop.diph.dwHow = DIPH_DEVICE;
					prop.dwData = DIPROPAUTOCENTER_OFF;
					d.dev->SetProperty(DIPROP_AUTOCENTER, &prop.diph);
					ffIndex = (int)devices.size();
				}
				d.dev->Acquire();
				d.state.connected = true;
				devices.push_back(d);
				log::info("input: DirectInput device %d '%s'%s", (int)devices.size() - 1, d.name.c_str(), wantFF ? " (force feedback)" : "");
			}

			if (ffDev()) {
				DWORD axes[1] = { (DWORD)DIJOFS_X };
				LONG dir[1] = { 0 };
				DICONSTANTFORCE cf{ 0 };
				DIEFFECT eff{};
				eff.dwSize = sizeof(DIEFFECT);
				eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
				eff.dwDuration = INFINITE;
				eff.dwGain = DI_FFNOMINALMAX;
				eff.dwTriggerButton = DIEB_NOTRIGGER;
				eff.cAxes = 1;
				eff.rgdwAxes = axes;
				eff.rglDirection = dir;
				eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
				eff.lpvTypeSpecificParams = &cf;
				if (SUCCEEDED(ffDev()->CreateEffect(GUID_ConstantForce, &eff, &constantFx, nullptr))) constantFx->Start(1, 0);
				springFx = makeCondition(GUID_Spring);
				damperFx = makeCondition(GUID_Damper);
			}
			else log::info("input: no force-feedback wheel found");
			return ffDev() != nullptr;
		}

		void shutdown() {
			stopAll();
			for (IDirectInputEffect** fx : { &constantFx, &springFx, &damperFx })
				if (*fx) { (*fx)->Stop(); (*fx)->Release(); *fx = nullptr; }
			for (Device& d : devices) if (d.dev) { d.dev->Unacquire(); d.dev->Release(); }
			devices.clear();
			found.clear();
			ffIndex = -1;
			if (di) { di->Release(); di = nullptr; }
		}

		bool wheelAvailable() { return ffDev() != nullptr; }
		const char* wheelName() { return ffIndex >= 0 ? devices[ffIndex].name.c_str() : ""; }

		void setWheel(float constant, float spring, float damper) {
			if (!ffDev()) return;
			LONG mag = (LONG)(std::clamp(constant, -1.0f, 1.0f) * DI_FFNOMINALMAX);
			if (constantFx && std::abs(mag - lastConstant) > 40) {
				lastConstant = mag;
				DWORD axes[1] = { (DWORD)DIJOFS_X };
				LONG dir[1] = { 0 };
				DICONSTANTFORCE cf{ mag };
				DIEFFECT eff{};
				eff.dwSize = sizeof(DIEFFECT);
				eff.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS;
				eff.cAxes = 1;
				eff.rgdwAxes = axes;
				eff.rglDirection = dir;
				eff.cbTypeSpecificParams = sizeof(DICONSTANTFORCE);
				eff.lpvTypeSpecificParams = &cf;
				constantFx->SetParameters(&eff, DIEP_TYPESPECIFICPARAMS | DIEP_START);
			}
			setCondition(springFx, (LONG)(std::clamp(spring, 0.0f, 1.0f) * DI_FFNOMINALMAX), lastSpring);
			setCondition(damperFx, (LONG)(std::clamp(damper, 0.0f, 1.0f) * DI_FFNOMINALMAX), lastDamper);
		}

		void setRumble(float low, float high) {
			if (!xinputSetState) return;
			WORD l = (WORD)(std::clamp(low, 0.0f, 1.0f) * 65535.0f), h = (WORD)(std::clamp(high, 0.0f, 1.0f) * 65535.0f);
			if (l == lastLow && h == lastHigh) return;
			lastLow = l; lastHigh = h;
			XVib v{ l, h };
			xinputSetState(0, &v);
		}

		void stopAll() {
			setRumble(0, 0);
			if (ffDev()) setWheel(0.0f, 0.25f, 0.1f);
		}
	}

	// ------------------------------------------------------------------
	namespace joy {
		int deviceCount() { return (int)devices.size(); }
		const char* deviceName(int i) { return (i >= 0 && i < (int)devices.size()) ? devices[i].name.c_str() : ""; }
		const State& state(int i) { return (i >= 0 && i < (int)devices.size()) ? devices[i].state : kEmpty; }

		void poll() {
			for (Device& d : devices) {
				if (!d.dev) continue;
				d.dev->Poll();
				DIJOYSTATE2 js{};
				HRESULT hr = d.dev->GetDeviceState(sizeof(DIJOYSTATE2), &js);
				if (FAILED(hr)) {
					d.dev->Acquire();
					d.state.connected = false;
					continue;
				}
				d.state.connected = true;
				LONG ax[kAxes] = { js.lX, js.lY, js.lZ, js.lRx, js.lRy, js.lRz, js.rglSlider[0], js.rglSlider[1] };
				for (int a = 0; a < kAxes; ++a) d.state.axis[a] = std::clamp(ax[a] / 10000.0f, -1.0f, 1.0f);
				for (int b = 0; b < kButtons; ++b) d.state.button[b] = (js.rgbButtons[b] & 0x80) != 0;
			}
		}
	}
}

#else
namespace rally {
	namespace ffb {
		bool init(void*) { return false; }
		void shutdown() {}
		bool wheelAvailable() { return false; }
		const char* wheelName() { return ""; }
		void setWheel(float, float, float) {}
		void setRumble(float, float) {}
		void stopAll() {}
	}
	namespace joy {
		static const State kEmpty{};
		int deviceCount() { return 0; }
		const char* deviceName(int) { return ""; }
		const State& state(int) { return kEmpty; }
		void poll() {}
	}
}
#endif
