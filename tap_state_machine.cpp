#include "tap_state_machine.h"

// prefix tsm stands for tap state machine

TSMStateMachine::TSMStateMachine(TSMStateMachineCallback *p_tsm_state_machine_callback_in)
    : p_tsm_state_machine_callback(p_tsm_state_machine_callback_in)
{
    // empty
}

void TSMStateMachine::tsm_reset()
{
    tsm_force_into_state(TEST_LOGIC_RESET);
}

void TSMStateMachine::tsm_force_into_state(tsm_state new_state)
{
    fprintf(stderr, "TSM tsm_force_into_state()\n");
    tsm_current_state = new_state;

    p_tsm_state_machine_callback->state_entered(tsm_current_state, 1);
}


/*
 ┌──┐                                                                             
 │ ┌┼─────────────────┐                                                           
1│ │ Test-Logic-Reset ◄──────────────────────────────────────────────────────┐    
 │ └▲───────┬─────────┘                                                      │    
 └──┘       │                                                                │    
            │0                                                               │1   
 ┌──┐       │                                                                │    
 │ ┌┼───────▼─────────┐  1   ┌────────────────┐  1       ┌─────────────────┐ │    
0│ │ Run-Test / Idle  ┼──────► Select-DR-Scan ┼──────────► Select-IR-Scan  ┼─┘    
 │ └▲──────▲──────────┘      └──▲────┬────────┘          └────────┬────────┘      
 └──┘      │     ┌──────────────┘    │0                           │0              
           │     │           ┌───────▼────────┐          ┌────────▼────────┐      
           │     │           │ Capture-DR     ┼┐        ┌┼ Capture-IR      │      
           │     │           └───────┬────────┘│1      1│└────────┬────────┘      
           │     │          ┌─┐      │0        │        │         │0      ┌─┐     
           │     │          │┌┼──────▼────────┐│        │┌────────▼───────┼┐│     
           │     │         0││ Shift-DR       ◄┼─┐    ┌─┼► Shift-IR        ││0    
           │     │          │└▲──────┬────────┘│ │    │ │└────────┬───────▲┘│     
           │     │          └─┘      │1        │ │    │ │         │1      └─┘     
           │     │           ┌───────▼────────┐│ │    │ │┌────────▼────────┐      
           │     │      ┌────┼ Exit1-DR       ◄┘ │    │ └► Exit1-IR        ┼────┐ 
           │     │     1│    └───────┬────────┘  │    │  └────────┬────────┘    │1
           │     │      │   ┌─┐      │0          │    │           │0      ┌─┐   │ 
           │     │      │   │┌┼──────▼────────┐  │    │  ┌────────▼───────┼┐│   │ 
           │     │      │  0││ Pause-DR       │  │    │  │ Pause-IR        ││0  │ 
           │     │      │   │└▲──────┬────────┘  │    │  └────────┬───────▲┘│   │ 
           │     │      │   └─┘      │1          │    │           │1      └─┘   │ 
           │     │      │    ┌───────▼────────┐  │0  0│  ┌────────▼────────┐    │ 
           │     │      │    │ Exit2-DR       ┼──┘    └──┼ Exit2-IR        │    │ 
           │     │      │    └───────┬────────┘          └────────┬────────┘    │ 
           │     │      │            │1                           │1            │ 
           │     │      │    ┌───────▼────────┐          ┌────────▼────────┐    │ 
           │     │      └────► Update-DR      │          │ Update-IR       ◄────┘ 
           │     │           └──┬──────────┬──┘          └──┬───────────┬──┘      
           │     │              │1         │0               │1          │0        
           │     └──────────────┴──────────┼────────────────┘           │         
           │                               │                            │         
           └───────────────────────────────┴────────────────────────────┘         
*/ 
void TSMStateMachine::transition(uint8_t input, uint8_t rising_edge_clk)
{
    // on the falling edge, the state machine remains in the current state
    if (rising_edge_clk == 0) {
        //fprintf(stderr, "falling_edge\n");
        p_tsm_state_machine_callback->state_entered(tsm_current_state, rising_edge_clk);
        return;
    }

    // on the rising edge, the TAP state machine transitions
    switch (tsm_current_state)
    {

    // In this state all test-modes (for example extest-mode) are reset, which will disable their operation, allowing the chip to follow its normal operation.
    case TEST_LOGIC_RESET:
        tsm_current_state = input ? TEST_LOGIC_RESET : RUN_TEST_IDLE;
        break;

    // This is the resting state during normal operation.
    case RUN_TEST_IDLE:
        tsm_current_state = input ? SELECT_DR_SCAN : RUN_TEST_IDLE;
        break;

    // These are the starting states respectively for accessing one of the data registers (the boundary-scan or bypass register in the minimal configuration) or the instruction register.
    case SELECT_DR_SCAN:
        tsm_current_state = input ? SELECT_IR_SCAN : CAPTURE_DR;
        break;
    case SELECT_IR_SCAN:
        tsm_current_state = input ? TEST_LOGIC_RESET : CAPTURE_IR;
        break;

    // These capture the current value of one of the data registers or the instruction register respectively into the scan cells. This is a slight misnomer for the instruction register, since it is usual to capture status information, rather than the actual instruction with Capture-IR.
    case CAPTURE_DR:
        tsm_current_state = input ? EXIT1_DR : SHIFT_DR;
        break;
    case CAPTURE_IR:
        tsm_current_state = input ? EXIT1_IR : SHIFT_IR;
        break;

    // Shift a bit in from TDI (on the rising edge of TCK) and out onto TDO (on the falling edge of TCK) from the currently selected data or instruction register respectively.
    case SHIFT_DR:
        tsm_current_state = input ? EXIT1_DR : SHIFT_DR;
        break;
    case SHIFT_IR:
        tsm_current_state = input ? EXIT1_IR : SHIFT_IR;
        break;

    // These are the exit states for the corresponding shift state. From here the state machine can either enter a pause state or enter the update state.
    case EXIT1_DR:
        tsm_current_state = input ? UPDATE_DR : PAUSE_DR;
        break;
    case EXIT1_IR:
        tsm_current_state = input ? UPDATE_IR : PAUSE_IR;
        break;

    // Pause in shifting data into the data or instruction register. This allows for example test equipment supplying TDO to reload buffers etc.
    case PAUSE_DR:
        tsm_current_state = input ? EXIT2_DR : PAUSE_DR;
        break;
    case PAUSE_IR:
        tsm_current_state = input ? EXIT2_IR : PAUSE_IR;
        break;

    // These are the exit states for the corresponding pause state. From here the state machine can either resume shifting or enter the update state.
    case EXIT2_DR:
        tsm_current_state = input ? UPDATE_DR : SHIFT_DR;
        break;
    case EXIT2_IR:
        tsm_current_state = input ? UPDATE_IR : SHIFT_IR;
        break;

    // The value shifted into the scan cells during the previous states is driven into the chip (from inputs) or onto the interconnect (for outputs).
    case UPDATE_DR:
        tsm_current_state = input ? SELECT_DR_SCAN : RUN_TEST_IDLE;
        break;
    case UPDATE_IR:
        tsm_current_state = input ? SELECT_DR_SCAN : RUN_TEST_IDLE;
        break;

    default:
        fprintf(stderr, "[Error] Unknown state!!!\n");
        return;
    }

    p_tsm_state_machine_callback->state_entered(tsm_current_state, rising_edge_clk);
}