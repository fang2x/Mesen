#include "stdafx.h"
#include "VsControlManager.h"
#include "VsSystem.h"

ControllerType VsControlManager::GetControllerType(uint8_t port)
{
	ControllerType type = ControlManager::GetControllerType(port);
	if(type == ControllerType::Zapper) {
		type = ControllerType::VsZapper;
	}
	return type;
}

void VsControlManager::Reset(bool softReset)
{
	ControlManager::Reset(softReset);
	_protectionCounter = 0;
	UpdateSlaveMasterBit(_console->IsMaster() ? 0x00 : 0x02);

	if(!softReset && !_console->IsMaster() && _console->GetDualConsole()) {
		RegisterInputProvider(this);
	}
}

void VsControlManager::StreamState(bool saving)
{
	ControlManager::StreamState(saving);

	VsInputType unusedInputType = VsInputType::Default;
	Stream(_prgChrSelectBit, _protectionCounter, _refreshState, unusedInputType);
}

void VsControlManager::GetMemoryRanges(MemoryRanges &ranges)
{
	ControlManager::GetMemoryRanges(ranges);
	ranges.AddHandler(MemoryOperation::Read, 0x4020, 0x5FFF);
	ranges.AddHandler(MemoryOperation::Write, 0x4020, 0x5FFF);
}

uint8_t VsControlManager::GetPrgChrSelectBit()
{
	return _prgChrSelectBit;
}

void VsControlManager::RemapControllerButtons()
{
	shared_ptr<StandardController> controllers[2];
	controllers[0] = std::dynamic_pointer_cast<StandardController>(GetControlDevice(0));
	controllers[1] = std::dynamic_pointer_cast<StandardController>(GetControlDevice(1));

	if(!controllers[0] || !controllers[1]) {
		return;
	}

	VsInputType inputType = EmulationSettings::GetVsInputType();
	if(inputType == VsInputType::TypeA) {
		BaseControlDevice::SwapButtons(controllers[0], StandardController::Buttons::Select, controllers[0], StandardController::Buttons::Start);
		BaseControlDevice::SwapButtons(controllers[1], StandardController::Buttons::Select, controllers[1], StandardController::Buttons::Start);
	} else if(inputType == VsInputType::TypeB) {
		std::swap(controllers[0], controllers[1]);
		BaseControlDevice::SwapButtons(controllers[1], StandardController::Buttons::Select, controllers[0], StandardController::Buttons::Start);
		BaseControlDevice::SwapButtons(controllers[0], StandardController::Buttons::Select, controllers[1], StandardController::Buttons::Start);
	} else if(inputType == VsInputType::TypeC) {
		std::swap(controllers[0], controllers[1]);

		if(controllers[0]->IsPressed(StandardController::Buttons::Start)) {
			controllers[1]->SetBit(StandardController::Buttons::Select);
		} else {
			controllers[1]->ClearBit(StandardController::Buttons::Select);
		}

		controllers[0]->ClearBit(StandardController::Buttons::Start);
		controllers[0]->ClearBit(StandardController::Buttons::Select);
	} else if(inputType == VsInputType::TypeD) {
		std::swap(controllers[0], controllers[1]);
		BaseControlDevice::SwapButtons(controllers[1], StandardController::Buttons::Select, controllers[0], StandardController::Buttons::Start);
		BaseControlDevice::SwapButtons(controllers[0], StandardController::Buttons::Select, controllers[1], StandardController::Buttons::Start);
		controllers[0]->InvertBit(StandardController::Buttons::Select);
		controllers[1]->InvertBit(StandardController::Buttons::Select);
	} else if(inputType == VsInputType::TypeE) {
		BaseControlDevice::SwapButtons(controllers[0], StandardController::Buttons::B, controllers[1], StandardController::Buttons::A);
		BaseControlDevice::SwapButtons(controllers[0], StandardController::Buttons::Select, controllers[0], StandardController::Buttons::Start);
		BaseControlDevice::SwapButtons(controllers[1], StandardController::Buttons::Select, controllers[1], StandardController::Buttons::Start);
	}
}

uint8_t VsControlManager::ReadRAM(uint16_t addr)
{
	uint8_t value = 0;

	uint32_t crc = _console->GetMapperInfo().Hash.PrgCrc32Hash;

	switch(addr) {
		case 0x4016: {
			uint32_t dipSwitches = EmulationSettings::GetDipSwitches();
			value = ControlManager::ReadRAM(addr);
			value |= ((dipSwitches & 0x01) ? 0x08 : 0x00);
			value |= ((dipSwitches & 0x02) ? 0x10 : 0x00);
			value |= (_console->IsMaster() ? 0x00 : 0x80);
			break;
		}

		case 0x4017: {
			value = ControlManager::ReadRAM(addr) & 0x01;

			uint32_t dipSwitches = EmulationSettings::GetDipSwitches();
			value |= ((dipSwitches & 0x04) ? 0x04 : 0x00);
			value |= ((dipSwitches & 0x08) ? 0x08 : 0x00);
			value |= ((dipSwitches & 0x10) ? 0x10 : 0x00);
			value |= ((dipSwitches & 0x20) ? 0x20 : 0x00);
			value |= ((dipSwitches & 0x40) ? 0x40 : 0x00);
			value |= ((dipSwitches & 0x80) ? 0x80 : 0x00);
			break;
		}

		case 0x5E00:
			_protectionCounter = 0;
			break;

		case 0x5E01:
			if(crc == 0xEB2DBA63 || crc == 0x98CFE016) {
				//TKO Boxing
				value = _protectionData[0][_protectionCounter++ & 0x1F];
			} else if(crc == 0x135ADF7C) {
				//RBI Baseball
				value = _protectionData[1][_protectionCounter++ & 0x1F];
			}
			break;

		default:
			if((crc == 0xF9D3B0A3 || crc == 0x66BB838F || crc == 0x9924980A) && addr >= 0x5400 && addr <= 0x57FF) {
				//Super devious
				return _protectionData[2][_protectionCounter++ & 0x1F];
			}
			break;
	}

	return value;
}

void VsControlManager::WriteRAM(uint16_t addr, uint8_t value)
{
	ControlManager::WriteRAM(addr, value);

	bool previousState = _refreshState;
	_refreshState = (value & 0x01) == 0x01;

	if(previousState && !_refreshState) {
		RemapControllerButtons();
	}

	if(addr == 0x4016) {
		_prgChrSelectBit = (value >> 2) & 0x01;
		
		//Bit 2: DualSystem-only
		uint8_t slaveMasterBit = (value & 0x02);
		if(slaveMasterBit != _slaveMasterBit) {
			UpdateSlaveMasterBit(slaveMasterBit);
		}
	}
}

void VsControlManager::UpdateSlaveMasterBit(uint8_t slaveMasterBit)
{
	shared_ptr<Console> dualConsole = _console->GetDualConsole();
	if(dualConsole) {
		VsSystem* mapper = dynamic_cast<VsSystem*>(_console->GetMapper());
		
		if(_console->IsMaster()) {
			mapper->UpdateMemoryAccess(slaveMasterBit);
		}

		if(slaveMasterBit) {
			dualConsole->GetCpu()->ClearIrqSource(IRQSource::External);
		} else {
			//When low, asserts /IRQ on the other CPU
			dualConsole->GetCpu()->SetIrqSource(IRQSource::External);
		}
	}
	_slaveMasterBit = slaveMasterBit;
}

void VsControlManager::UpdateControlDevices()
{
	if(_console->GetDualConsole()) {
		auto lock = _deviceLock.AcquireSafe();
		_controlDevices.clear();
		RegisterControlDevice(_systemActionManager);

		//Force 4x standard controllers
		//P3 & P4 will be sent to the slave CPU - see SetInput() below.
		for(int i = 0; i < 4; i++) {
			shared_ptr<BaseControlDevice> device = CreateControllerDevice(ControllerType::StandardController, i, _console);
			if(device) {
				RegisterControlDevice(device);
			}
		}
	} else {
		ControlManager::UpdateControlDevices();
	}
}

bool VsControlManager::SetInput(BaseControlDevice* device)
{
	uint8_t port = device->GetPort();
	ControlManager* masterControlManager = _console->GetDualConsole()->GetControlManager();
	if(masterControlManager && port <= 1) {
		shared_ptr<BaseControlDevice> controlDevice = masterControlManager->GetControlDevice(port + 2);
		if(controlDevice) {
			ControlDeviceState state = controlDevice->GetRawState();
			device->SetRawState(state);
		}
	}
	return true;
}