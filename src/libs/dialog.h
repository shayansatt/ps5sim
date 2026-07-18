#ifndef EMULATOR_INCLUDE_EMULATOR_DIALOG_H_
#define EMULATOR_INCLUDE_EMULATOR_DIALOG_H_

#include "common/abi.h"
#include "common/common.h"

namespace Libs::Dialog {

namespace CommonDialog {

int PS5SIM_SYSV_ABI CommonDialogInitialize();

} // namespace CommonDialog

namespace ImeDialog {

int PS5SIM_SYSV_ABI ImeDialogGetStatus();

} // namespace ImeDialog

namespace LoginDialog {

int PS5SIM_SYSV_ABI  LoginDialogInitialize();
int PS5SIM_SYSV_ABI  LoginDialogTerminate();
int PS5SIM_SYSV_ABI  LoginDialogOpen(const void* param);
int PS5SIM_SYSV_ABI  LoginDialogClose();
int PS5SIM_SYSV_ABI  LoginDialogUpdateStatus();
int PS5SIM_SYSV_ABI  LoginDialogGetStatus();
int PS5SIM_SYSV_ABI  LoginDialogGetResult(void* result);
void PS5SIM_SYSV_ABI LoginDialogParamInitialize(void* param);

} // namespace LoginDialog

namespace SaveDataDialog {

int PS5SIM_SYSV_ABI SaveDataDialogInitialize();
int PS5SIM_SYSV_ABI SaveDataDialogGetStatus();
int PS5SIM_SYSV_ABI SaveDataDialogUpdateStatus();
int PS5SIM_SYSV_ABI SaveDataDialogGetResult(void* result);
int PS5SIM_SYSV_ABI SaveDataDialogOpen(const void* param);
int PS5SIM_SYSV_ABI SaveDataDialogClose(const void* close_param);
int PS5SIM_SYSV_ABI SaveDataDialogIsReadyToDisplay();
int PS5SIM_SYSV_ABI SaveDataDialogTerminate();
int PS5SIM_SYSV_ABI SaveDataDialogProgressBarInc(int target, uint32_t delta);
int PS5SIM_SYSV_ABI SaveDataDialogProgressBarSetValue(int target, uint32_t rate);

} // namespace SaveDataDialog

namespace MsgDialog {

int PS5SIM_SYSV_ABI MsgDialogInitialize();
int PS5SIM_SYSV_ABI MsgDialogOpen(const void* param);
int PS5SIM_SYSV_ABI MsgDialogUpdateStatus();
int PS5SIM_SYSV_ABI MsgDialogGetStatus();
int PS5SIM_SYSV_ABI MsgDialogGetResult(void* result);
int PS5SIM_SYSV_ABI MsgDialogTerminate();
int PS5SIM_SYSV_ABI MsgDialogClose();
int PS5SIM_SYSV_ABI MsgDialogProgressBarInc(int target, uint32_t delta);
int PS5SIM_SYSV_ABI MsgDialogProgressBarSetValue(int target, uint32_t rate);
int PS5SIM_SYSV_ABI MsgDialogProgressBarSetMsg(int target, const char* msg);

} // namespace MsgDialog

namespace ErrorDialog {

int PS5SIM_SYSV_ABI ErrorDialogInitialize();
int PS5SIM_SYSV_ABI ErrorDialogOpen(const void* param);
int PS5SIM_SYSV_ABI ErrorDialogClose();
int PS5SIM_SYSV_ABI ErrorDialogTerminate();
int PS5SIM_SYSV_ABI ErrorDialogUpdateStatus();
int PS5SIM_SYSV_ABI ErrorDialogGetStatus();

} // namespace ErrorDialog

} // namespace Libs::Dialog

#endif /* EMULATOR_INCLUDE_EMULATOR_DIALOG_H_ */
