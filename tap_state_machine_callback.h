#ifndef TAP_STATEMACHINE_CALLBACK_H
#define TAP_STATEMACHINE_CALLBACK_H

// see https://www.embecosm.com/appnotes/ean5/html/ch02s01s02.html
enum tsm_state {

  // In this state all test-modes (for example extest-mode) are reset, which will disable their operation, allowing the chip to follow its normal operation.
  TEST_LOGIC_RESET,

  // This is the resting state during normal operation.
  RUN_TEST_IDLE, 
  
  // These are the starting states respectively for accessing one of the data registers (the boundary-scan or bypass register in the minimal configuration) or the instruction register.
  SELECT_DR_SCAN,
  SELECT_IR_SCAN, 
  
  // These capture the current value of one of the data registers or the instruction register respectively into the scan cells. This is a slight misnomer for the instruction register, since it is usual to capture status information, rather than the actual instruction with Capture-IR.
  CAPTURE_DR,
  CAPTURE_IR, 
  
  // Shift a bit in from TDI (on the rising edge of TCK) and out onto TDO (on the falling edge of TCK) from the currently selected data or instruction register respectively.
  SHIFT_DR, 
  SHIFT_IR, 

  // These are the exit states for the corresponding shift state. From here the state machine can either enter a pause state or enter the update state.
  EXIT1_DR, 
  EXIT1_IR, 

  // Pause in shifting data into the data or instruction register. This allows for example test equipment supplying TDO to reload buffers etc.
  PAUSE_DR,
  PAUSE_IR,

  // These are the exit states for the corresponding pause state. From here the state machine can either resume shifting or enter the update state.
  EXIT2_DR,
  EXIT2_IR,

  // The value shifted into the scan cells during the previous states is driven into the chip (from inputs) or onto the interconnect (for outputs).
  UPDATE_DR,
  UPDATE_IR
};

class TSMStateMachineCallback {

public:

    virtual ~TSMStateMachineCallback() {}
    
    virtual void state_entered(tsm_state new_state) = 0;

};

#endif