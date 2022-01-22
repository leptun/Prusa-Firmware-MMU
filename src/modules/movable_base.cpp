/// @file movable_base.cpp
#include "movable_base.h"
#include "globals.h"
#include "motion.h"

namespace modules {
namespace motion {

void MovableBase::PlanHome() {
    // switch to normal mode on this axis
    mm::motion.InitAxis(axis);
    mm::motion.SetMode(axis, mm::Normal);
    mm::motion.StallGuardReset(axis);

    // plan move at least as long as the axis can go from one side to the other
    PlanHomingMoveForward(); // mm::motion.PlanMove(axis, delta, 1000);
    state = HomeForward;
}

MovableBase::OperationResult MovableBase::InitMovement() {
    if (motion.InitAxis(axis)) {
        PrepareMoveToPlannedSlot();
        state = Moving;
        return OperationResult::Accepted;
    } else {
        state = TMCFailed;
        return OperationResult::Failed;
    }
}

void MovableBase::PerformMove() {
    if (!mm::motion.DriverForAxis(axis).GetErrorFlags().Good()) { // @@TODO check occasionally, i.e. not every time?
        // TMC2130 entered some error state, the planned move couldn't have been finished - result of operation is Failed
        tmcErrorFlags = mm::motion.DriverForAxis(axis).GetErrorFlags(); // save the failed state
        state = TMCFailed;
    } else if (mm::motion.QueueEmpty(axis)) {
        // move finished
        currentSlot = plannedSlot;
        FinishMove();
        state = Ready;
    }
}

void MovableBase::PerformHomeForward() {
    if (mm::motion.StallGuard(axis)) {
        // we have reached the front end of the axis - first part homed probably ok
        mm::motion.StallGuardReset(axis);
        mm::motion.AbortPlannedMoves(axis, true);
        PlanHomingMoveBack();
        state = HomeBack;
    } else if (mm::motion.QueueEmpty(axis)) {
        HomeFailed();
    }
}

void MovableBase::PerformHomeBack() {
    if (mm::motion.StallGuard(axis)) {
        // we have reached the back end of the axis - second part homed probably ok
        mm::motion.StallGuardReset(axis);
        mm::motion.AbortPlannedMoves(axis, true);
        mm::motion.SetMode(axis, mg::globals.MotorsStealth() ? mm::Stealth : mm::Normal);
        if (!FinishHomingAndPlanMoveToParkPos()) {
            // the measured axis' length was incorrect, something is blocking it, report an error, homing procedure terminated
            state = HomingFailed;
        } else {
            homingValid = true;
            // state = Ready; // not yet - we have to move to our parking position after homing the axis
        }
    } else if (mm::motion.QueueEmpty(axis)) {
        HomeFailed();
    }
}

void MovableBase::HomeFailed() {
    // we ran out of planned moves but no StallGuard event has occurred - homing failed
    homingValid = false;
    mm::motion.SetMode(axis, mg::globals.MotorsStealth() ? mm::Stealth : mm::Normal);
    state = HomingFailed;
}

} // namespace motion
} // namespace modules
