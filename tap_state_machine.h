#ifndef TAP_STATEMACHINE_H
#define TAP_STATEMACHINE_H

#include <stdint.h>
#include <string.h>
#include <cstdio>
#include <cstdlib>

#include "tap_state_machine_callback.h"

// prefix tsm stands for tap state machine

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



class TSMStateMachine {

    private:
        TSMStateMachineCallback* p_tsm_state_machine_callback;

        tsm_state tsm_current_state;

    public:

        TSMStateMachine(TSMStateMachineCallback* p_tsm_state_machine_callback);

        void tsm_reset();

        void tsm_force_into_state(tsm_state new_state);

        void transition(uint8_t input);

};

#endif