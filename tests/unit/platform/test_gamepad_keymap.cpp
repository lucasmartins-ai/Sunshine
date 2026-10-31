/**
 * @file tests/unit/platform/test_gamepad_keymap.cpp
 * @brief Test src/platform/gamepad_keymap.*.
 */

// test includes
#include "../../tests_common.h"

// standard includes
#include <vector>

// lib includes
#include <moonlight-common-c/src/Limelight.h>

// local includes
#include "src/platform/gamepad_keymap.h"

using platf::gamepad_state_t;
using platf::keymap::default_keymap;
using platf::keymap::key_event_t;
using platf::keymap::mapper_t;
using platf::keymap::parse_keymap;

namespace {

  /**
   * @brief Build a gamepad state with only the left stick deflected.
   *
   * @param x Left stick X-axis value.
   * @param y Left stick Y-axis value.
   * @return Gamepad state with the stick deflected.
   */
  gamepad_state_t left_stick(std::int16_t x, std::int16_t y) {
    return gamepad_state_t {0, 0, 0, x, y, 0, 0};
  }

  /**
   * @brief Build a gamepad state with only buttons held.
   *
   * @param flags Moonlight button flags.
   * @return Gamepad state with the buttons held.
   */
  gamepad_state_t buttons(std::uint32_t flags) {
    return gamepad_state_t {flags, 0, 0, 0, 0, 0, 0};
  }

}  // namespace

TEST(GamepadKeymapTest, LeftStickPressesAndReleasesWasdIncludingDiagonals) {
  mapper_t mapper {default_keymap()};

  EXPECT_EQ(mapper.update(left_stick(0, 32767)), (std::vector<key_event_t> {{'W', false}}));
  EXPECT_EQ(mapper.update(left_stick(32767, 32767)), (std::vector<key_event_t> {{'D', false}}));
  EXPECT_EQ(mapper.update(left_stick(0, 0)), (std::vector<key_event_t> {{'W', true}, {'D', true}}));
  EXPECT_TRUE(mapper.update(left_stick(0, 0)).empty());
}

TEST(GamepadKeymapTest, FullNegativeDeflectionDoesNotOverflow) {
  mapper_t mapper {default_keymap()};

  EXPECT_EQ(mapper.update(left_stick(-32768, -32768)), (std::vector<key_event_t> {{'S', false}, {'A', false}}));
}

TEST(GamepadKeymapTest, StickHysteresisKeepsAHeldDirectionDown) {
  mapper_t mapper {default_keymap()};

  EXPECT_TRUE(mapper.update(left_stick(0, 14000)).empty()) << "below the press threshold from rest";
  EXPECT_EQ(mapper.update(left_stick(0, 20000)), (std::vector<key_event_t> {{'W', false}}));
  EXPECT_TRUE(mapper.update(left_stick(0, 14000)).empty()) << "above the release threshold while held";
  EXPECT_EQ(mapper.update(left_stick(0, 10000)), (std::vector<key_event_t> {{'W', true}}));
}

TEST(GamepadKeymapTest, ButtonsAndTriggersMapToTheirKeys) {
  mapper_t mapper {default_keymap()};

  EXPECT_EQ(mapper.update(buttons(A_FLAG | UP_FLAG)), (std::vector<key_event_t> {{0x20, false}, {0x26, false}}));
  EXPECT_EQ(mapper.update(buttons(0)), (std::vector<key_event_t> {{0x20, true}, {0x26, true}}));

  EXPECT_TRUE(mapper.update(gamepad_state_t {0, 100, 0, 0, 0, 0, 0}).empty());
  EXPECT_EQ(mapper.update(gamepad_state_t {0, 200, 0, 0, 0, 0, 0}), (std::vector<key_event_t> {{'Q', false}}));
}

TEST(GamepadKeymapTest, KeySharedByTwoInputsStaysDownUntilBothRelease) {
  const auto keymap = parse_keymap("dpad_up=w");
  ASSERT_TRUE(keymap);
  mapper_t mapper {*keymap};

  EXPECT_EQ(mapper.update(gamepad_state_t {UP_FLAG, 0, 0, 0, 32767, 0, 0}), (std::vector<key_event_t> {{'W', false}}));
  EXPECT_TRUE(mapper.update(left_stick(0, 32767)).empty()) << "the stick still holds W";
  EXPECT_EQ(mapper.update(left_stick(0, 0)), (std::vector<key_event_t> {{'W', true}}));
}

TEST(GamepadKeymapTest, KeyPassedBetweenInputsInOneStateStaysDown) {
  const auto keymap = parse_keymap("b=space");
  ASSERT_TRUE(keymap);
  mapper_t mapper {*keymap};

  EXPECT_EQ(mapper.update(buttons(A_FLAG)), (std::vector<key_event_t> {{0x20, false}}));
  EXPECT_TRUE(mapper.update(buttons(B_FLAG)).empty()) << "space never goes up between A and B";
  EXPECT_EQ(mapper.update(buttons(0)), (std::vector<key_event_t> {{0x20, true}}));
}

TEST(GamepadKeymapTest, ReleaseAllReleasesHeldKeys) {
  mapper_t mapper {default_keymap()};
  mapper.update(gamepad_state_t {B_FLAG, 0, 255, 0, 32767, 0, 0});

  EXPECT_EQ(mapper.release_all(), (std::vector<key_event_t> {{'F', true}, {'E', true}, {'W', true}}));
  EXPECT_TRUE(mapper.release_all().empty());
}

TEST(GamepadKeymapTest, ParseKeymapAppliesOverridesOnTopOfDefaults) {
  const auto keymap = parse_keymap(" A = Escape , rs_up=none,start=f5, home=0x5B ,");
  ASSERT_TRUE(keymap);

  auto expected = default_keymap();
  expected[static_cast<std::size_t>(platf::keymap::input_e::a)] = 0x1B;
  expected[static_cast<std::size_t>(platf::keymap::input_e::rs_up)] = 0;
  expected[static_cast<std::size_t>(platf::keymap::input_e::start)] = 0x74;
  expected[static_cast<std::size_t>(platf::keymap::input_e::home)] = 0x5B;
  EXPECT_EQ(*keymap, expected);

  EXPECT_EQ(parse_keymap(""), default_keymap());
}

TEST(GamepadKeymapTest, ParseKeymapRejectsMalformedEntries) {
  EXPECT_FALSE(parse_keymap("a"));
  EXPECT_FALSE(parse_keymap("trigger=space"));
  EXPECT_FALSE(parse_keymap("a=nokey"));
  EXPECT_FALSE(parse_keymap("a=f13"));
  EXPECT_FALSE(parse_keymap("a=0x1FF"));
}

TEST(GamepadKeymapTest, ActiveKeymapPrefersTheStreamedApplication) {
  platf::keymap::set_app_keymap("a=x");
  EXPECT_EQ(platf::keymap::active_keymap_spec("b=z"), "a=x");

  platf::keymap::set_app_keymap({});
  EXPECT_EQ(platf::keymap::active_keymap_spec("b=z"), "b=z");
}
