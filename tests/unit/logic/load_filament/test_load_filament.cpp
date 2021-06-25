#include "catch2/catch.hpp"

#include "../../../../src/modules/buttons.h"
#include "../../../../src/modules/finda.h"
#include "../../../../src/modules/fsensor.h"
#include "../../../../src/modules/globals.h"
#include "../../../../src/modules/idler.h"
#include "../../../../src/modules/leds.h"
#include "../../../../src/modules/motion.h"
#include "../../../../src/modules/permanent_storage.h"
#include "../../../../src/modules/selector.h"

#include "../../../../src/logic/load_filament.h"

#include "../../modules/stubs/stub_adc.h"

#include "../stubs/main_loop_stub.h"
#include "../stubs/stub_motion.h"

using Catch::Matchers::Equals;

namespace mm = modules::motion;
namespace mf = modules::finda;
namespace mi = modules::idler;
namespace ml = modules::leds;
namespace mb = modules::buttons;
namespace mg = modules::globals;
namespace ms = modules::selector;

#include "../helpers/helpers.ipp"

void LoadFilamentCommonSetup(uint8_t slot, logic::LoadFilament &lf) {
    ForceReinitAllAutomata();

    // change the startup to what we need here
    EnsureActiveSlotIndex(slot);

    // verify startup conditions
    REQUIRE(VerifyState(lf, false, 5, slot, false, ml::off, ml::off, ErrorCode::OK, ProgressCode::OK));

    // restart the automaton
    lf.Reset(slot);

    // Stage 0 - verify state just after Reset()
    // we assume the filament is not loaded
    // idler should have been activated by the underlying automaton
    // no change in selector's position
    // FINDA off
    // green LED should blink, red off
    REQUIRE(VerifyState(lf, false, 5, slot, false, ml::blink0, ml::off, ErrorCode::OK, ProgressCode::EngagingIdler));

    // Stage 1 - engaging idler
    REQUIRE(WhileTopState(lf, ProgressCode::EngagingIdler, 5000));
    REQUIRE(VerifyState(lf, false, slot, slot, false, ml::blink0, ml::off, ErrorCode::OK, ProgressCode::FeedingToFinda));
}

void LoadFilamentSuccessful(uint8_t slot, logic::LoadFilament &lf) {
    // Stage 2 - feeding to finda
    // we'll assume the finda is working correctly here
    REQUIRE(WhileCondition(
        lf,
        [&](int step) -> bool {
        if(step == 100){ // on 100th step make FINDA trigger
            hal::adc::SetADC(1, 1023);
        }
        return lf.TopLevelState() == ProgressCode::FeedingToFinda; },
        5000));
    REQUIRE(VerifyState(lf, false, slot, slot, true, ml::blink0, ml::off, ErrorCode::OK, ProgressCode::FeedingToBondtech));

    // Stage 3 - feeding to bondtech
    // we'll make a fsensor switch during the process
    REQUIRE(WhileCondition(
        lf,
        [&](int step) -> bool {
        if(step == 100){ // on 100th step make fsensor trigger
            modules::fsensor::fsensor.ProcessMessage(true);
        }
        return lf.TopLevelState() == ProgressCode::FeedingToBondtech; },
        5000));
    REQUIRE(VerifyState(lf, false, slot, slot, true, ml::blink0, ml::off, ErrorCode::OK, ProgressCode::DisengagingIdler));

    // Stage 4 - disengaging idler
    REQUIRE(WhileTopState(lf, ProgressCode::DisengagingIdler, 5000));
    REQUIRE(VerifyState(lf, true, 5, slot, true, ml::on, ml::off, ErrorCode::OK, ProgressCode::OK));
}

TEST_CASE("load_filament::regular_load_to_slot_0-4", "[load_filament]") {
    for (uint8_t slot = 0; slot < 5; ++slot) {
        logic::LoadFilament lf;
        LoadFilamentCommonSetup(slot, lf);
        LoadFilamentSuccessful(slot, lf);
    }
}

void FailedLoadToFinda(uint8_t slot, logic::LoadFilament &lf) {
    // Stage 2 - feeding to finda
    // we'll assume the finda is defective here and does not trigger
    REQUIRE(WhileTopState(lf, ProgressCode::FeedingToFinda, 5000));
    REQUIRE(VerifyState(lf, false, slot, slot, false, ml::off, ml::blink0, ErrorCode::FINDA_DIDNT_TRIGGER, ProgressCode::ERR1DisengagingIdler));

    // Stage 3 - disengaging idler in error mode
    REQUIRE(WhileTopState(lf, ProgressCode::ERR1DisengagingIdler, 5000));
    REQUIRE(VerifyState(lf, false, 5, slot, false, ml::off, ml::blink0, ErrorCode::FINDA_DIDNT_TRIGGER, ProgressCode::ERR1WaitingForUser));
}

void FailedLoadToFindaResolveHelp(uint8_t slot, logic::LoadFilament &lf) {
    // Stage 3 - the user has to do something
    // there are 3 options:
    // - help the filament a bit
    // - try again the whole sequence
    // - resolve the problem by hand - after pressing the button we shall check, that FINDA is off and we should do what?

    // In this case we check the first option

    // Perform press on button 1 + debounce
    hal::adc::SetADC(0, 0);
    while (!mb::buttons.ButtonPressed(0)) {
        main_loop();
        lf.Step();
    }

    REQUIRE(VerifyState(lf, false, 5, slot, false, ml::off, ml::blink0, ErrorCode::FINDA_DIDNT_TRIGGER, ProgressCode::ERR1EngagingIdler));

    // Stage 4 - engage the idler
    REQUIRE(WhileTopState(lf, ProgressCode::ERR1EngagingIdler, 5000));

    REQUIRE(VerifyState(lf, false, slot, slot, false, ml::off, ml::blink0, ErrorCode::FINDA_DIDNT_TRIGGER, ProgressCode::ERR1HelpingFilament));
}

void FailedLoadToFindaResolveHelpFindaTriggered(uint8_t slot, logic::LoadFilament &lf) {
    // Stage 5 - move the pulley a bit - simulate FINDA depress
    REQUIRE(WhileCondition(
        lf,
        [&](int step) -> bool {
        if(step == 100){ // on 100th step make FINDA trigger
            hal::adc::SetADC(1, 1023);
        }
        return lf.TopLevelState() == ProgressCode::ERR1HelpingFilament; },
        5000));

    REQUIRE(VerifyState(lf, false, slot, slot, true, ml::off, ml::blink0, ErrorCode::OK, ProgressCode::FeedingToBondtech));
}

void FailedLoadToFindaResolveHelpFindaDidntTrigger(uint8_t slot, logic::LoadFilament &lf) {
    // Stage 5 - move the pulley a bit - no FINDA change
    REQUIRE(WhileTopState(lf, ProgressCode::ERR1HelpingFilament, 5000));

    REQUIRE(VerifyState(lf, false, slot, slot, false, ml::off, ml::blink0, ErrorCode::FINDA_DIDNT_TRIGGER, ProgressCode::ERR1DisengagingIdler));
}

TEST_CASE("load_filament::failed_load_to_finda_0-4_resolve_help_second_ok", "[load_filament]") {
    for (uint8_t slot = 0; slot < 5; ++slot) {
        logic::LoadFilament lf;
        LoadFilamentCommonSetup(slot, lf);
        FailedLoadToFinda(slot, lf);
        FailedLoadToFindaResolveHelp(slot, lf);
        FailedLoadToFindaResolveHelpFindaTriggered(slot, lf);
    }
}

TEST_CASE("load_filament::failed_load_to_finda_0-4_resolve_help_second_fail", "[load_filament]") {
    for (uint8_t slot = 0; slot < 5; ++slot) {
        logic::LoadFilament lf;
        LoadFilamentCommonSetup(slot, lf);
        FailedLoadToFinda(slot, lf);
        FailedLoadToFindaResolveHelp(slot, lf);
        FailedLoadToFindaResolveHelpFindaDidntTrigger(slot, lf);
    }
}