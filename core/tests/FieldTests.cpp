#include "Test.h"
#include "cricket/FieldPlacement.h"

using namespace cricket;

TEST(standard_positions_geometry) {
    for (const auto& p : StandardPositions()) {
        CHECK(!OnPitch(p.pos));
        CHECK(DistanceFromCentre(p.pos) < kDefaultBoundary - 1.0);
    }
    CHECK(InsideCircle(FindPosition("First slip")->pos));
    CHECK(InsideCircle(FindPosition("Mid-off")->pos));
    CHECK(!InsideCircle(FindPosition("Long-on")->pos));
    CHECK(!InsideCircle(FindPosition("Third man")->pos));
    CHECK(BehindSquareLeg(FindPosition("Fine leg")->pos));
    CHECK(BehindSquareLeg(FindPosition("Leg slip")->pos));
    CHECK(!BehindSquareLeg(FindPosition("Square leg")->pos));
    CHECK(!BehindSquareLeg(FindPosition("Gully")->pos));
}

TEST(every_preset_has_nine_fielders_and_is_legal_after_powerplay) {
    MatchRules t20 = MatchRules::T20();
    for (FieldPreset p : AllPresets()) {
        FieldSetup f = FieldSetup::FromPreset(p);
        CHECK_EQ(f.fielders.size(), size_t{9});
        FieldCheck c = ValidateField(f, {&t20, 15});
        if (!c.status) std::cerr << "  preset " << ToString(p) << ": " << c.status.error << "\n";
        CHECK(c.status.ok);
    }
}

TEST(powerplay_restriction) {
    MatchRules t20 = MatchRules::T20();
    CHECK_ERR(ValidateField(FieldSetup::FromPreset(FieldPreset::DeathOvers), {&t20, 3}).status);
    CHECK_OK(ValidateField(FieldSetup::FromPreset(FieldPreset::Powerplay), {&t20, 3}).status);
    CHECK_OK(ValidateField(FieldSetup::FromPreset(FieldPreset::DeathOvers), {&t20, 19}).status);

    MatchRules odi = MatchRules::ODI();
    // Death field has 5 out: illegal in middle overs (max 4), legal in 41-50.
    CHECK_ERR(ValidateField(FieldSetup::FromPreset(FieldPreset::DeathOvers), {&odi, 25}).status);
    CHECK_OK(ValidateField(FieldSetup::FromPreset(FieldPreset::DeathOvers), {&odi, 45}).status);
    CHECK_OK(ValidateField(FieldSetup::FromPreset(FieldPreset::Defensive), {&odi, 25}).status);

    MatchRules test = MatchRules::Test();
    CHECK_OK(ValidateField(FieldSetup::FromNames({"Third man", "Deep point", "Deep cover", "Long-off", "Long-on",
                                                   "Cow corner", "Deep midwicket", "Deep square leg", "Fine leg"}),
                           {&test, 30})
                 .status);
}

TEST(max_two_behind_square_on_leg_side) {
    FieldSetup f = FieldSetup::FromPreset(FieldPreset::LegSideAttack);
    CHECK_OK(ValidateField(f, {}).status);
    // Swap point for a leg slip -> 3 behind square.
    for (auto& fp : f.fielders)
        if (fp.name == "Point") fp = *FindPosition("Leg slip");
    FieldCheck c = ValidateField(f, {});
    CHECK_ERR(c.status);
    CHECK_EQ(c.legSideBehindSquare, 3);
}

TEST(manual_placement_checks) {
    FieldSetup f = FieldSetup::FromPreset(FieldPreset::Balanced);
    CHECK_OK(f.Move(0, {0.0, 10.0}, "On the pitch"));
    CHECK_ERR(ValidateField(f, {}).status);
    f = FieldSetup::FromPreset(FieldPreset::Balanced);
    CHECK_OK(f.Move(0, {0.0, 120.0}, "In the car park"));
    CHECK_ERR(ValidateField(f, {}).status);
    f = FieldSetup::FromPreset(FieldPreset::Balanced);
    f.Move(0, f.fielders[1].pos, "Stacked");
    CHECK_ERR(ValidateField(f, {}).status);
    CHECK_ERR(f.Move(9, {10, 10}));
    f.fielders.pop_back();
    CHECK_ERR(ValidateField(f, {}).status);
}
