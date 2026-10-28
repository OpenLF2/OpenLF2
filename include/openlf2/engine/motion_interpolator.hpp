// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/ports/platform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace openlf2 {
// Pictures between two script frames, for display rates above the frame rate. The picture of the frame before the
// newest is kept, and each of its motion groups is moved part of the way to where the same group is in the newest
// frame. The shown picture runs one frame behind the scripts and always moves forward; at phase 0 it is exactly the
// older frame's picture, at phase 1 it has the newer frame's positions.
class MotionInterpolator {
public:
    // A group that moved further than this between two frames (a respawn, a camera cut) is not moved.
    static constexpr int max_step = 80;

    void clear() {
        previous_.reset();
        current_.reset();
        moves_.clear();
        origins_.clear();
        ready_ = false;
    }
    // Takes the newest frame's drawing.
    void push(std::vector<DrawCommand> commands, std::vector<MotionGroup> groups, const Viewport& viewport) {
        previous_ = std::move(current_);
        current_ = Frame{std::move(commands), std::move(groups), viewport};
        pair_groups();
        dirty_ = true;
    }
    // Whether there is something to move: two frames with the same viewport and groups that exist in both.
    [[nodiscard]] bool ready() const { return ready_; }
    // Moves the groups `phase` (0..1) of the way; false when the picture is the same as after the last call.
    bool move(double phase) {
        if (!ready_) return false;
        bool changed = dirty_;
        dirty_ = false;
        for (auto& group : moves_) {
            const int x = static_cast<int>(std::lround(group.dx * phase));
            const int y = static_cast<int>(std::lround(group.dy * phase));
            if (x == group.x && y == group.y) continue;
            group.x = x;
            group.y = y;
            changed = true;
            for (std::size_t index = group.first; index < group.last; ++index) {
                const auto& origin = origins_[group.origin + index - group.first];
                place(previous_->commands[index], origin[0] + x, origin[1] + y);
            }
        }
        return changed;
    }
    // What to present: the moved older picture when ready(), else the newest frame as it is.
    [[nodiscard]] const std::vector<DrawCommand>& commands() const { return ready_ ? previous_->commands : current_->commands; }
    [[nodiscard]] const Viewport& viewport() const { return ready_ ? previous_->viewport : current_->viewport; }

private:
    struct Frame {
        std::vector<DrawCommand> commands;
        std::vector<MotionGroup> groups;
        Viewport viewport;
    };
    struct Move {
        std::size_t first, last;
        int dx, dy; // how far the group moves in all
        int x = 0, y = 0; // how far it is moved now
        std::size_t origin; // where its commands' positions start in origins_
    };
    static std::array<int, 2> position(const DrawCommand& command) {
        return std::visit([](const auto& value) -> std::array<int, 2> {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, FillCommand>) return {value.area.x, value.area.y};
            else return {value.x, value.y};
        }, command);
    }
    static void place(DrawCommand& command, int x, int y) {
        std::visit([&](auto& value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, FillCommand>) { value.area.x = x; value.area.y = y; }
            else { value.x = x; value.y = y; }
        }, command);
    }
    static bool same_area(const Viewport& a, const Viewport& b) {
        if (a.width != b.width || a.height != b.height || a.render_area.has_value() != b.render_area.has_value()) return false;
        if (!a.render_area) return true;
        return a.render_area->x == b.render_area->x && a.render_area->y == b.render_area->y
            && a.render_area->width == b.render_area->width && a.render_area->height == b.render_area->height;
    }
    void pair_groups() {
        moves_.clear();
        origins_.clear();
        ready_ = false;
        if (!previous_ || !current_ || !same_area(previous_->viewport, current_->viewport)) return;
        newer_ = current_->groups;
        std::ranges::sort(newer_, {}, &MotionGroup::id);
        for (const auto& group : previous_->groups) {
            const auto found = std::ranges::lower_bound(newer_, group.id, {}, &MotionGroup::id);
            if (found == newer_.end() || found->id != group.id) continue;
            if (group.first == group.last) continue;
            const int dx = found->x - group.x;
            const int dy = found->y - group.y;
            if (std::abs(dx) > max_step || std::abs(dy) > max_step) continue;
            moves_.push_back({group.first, group.last, dx, dy, 0, 0, origins_.size()});
            for (std::size_t index = group.first; index < group.last; ++index) {
                origins_.push_back(position(previous_->commands[index]));
            }
        }
        ready_ = !moves_.empty();
    }

    std::optional<Frame> previous_, current_;
    std::vector<Move> moves_;
    std::vector<std::array<int, 2>> origins_;
    std::vector<MotionGroup> newer_;
    bool ready_ = false;
    bool dirty_ = false;
};
}
