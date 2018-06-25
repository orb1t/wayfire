#include <output.hpp>
#include <core.hpp>
#include <debug.hpp>
#include <view.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <queue>
#include <linux/input.h>
#include <utility>
#include <animation.hpp>
#include <set>
#include "view-change-viewport-signal.hpp"


#define MAX_DIRS_IN_QUEUE 4

struct switch_direction
{
    int dx, dy;
    wayfire_view view;
};

class vswitch : public wayfire_plugin_t
{
    private:
        key_callback callback_left, callback_right, callback_up, callback_down;
        key_callback callback_win_left, callback_win_right, callback_win_up, callback_win_down;

        touch_gesture_callback gesture_cb;

        std::queue<switch_direction> dirs; // series of moves we have to do

        wf_duration duration;
        wf_option animation_duration;

        bool running = false;
        effect_hook_t hook;

    public:

    void init(wayfire_config *config) {
        grab_interface->name = "vswitch";
        grab_interface->abilities_mask = WF_ABILITY_CONTROL_WM;

        callback_left = [=] (uint32_t) { add_direction(-1, 0); };
        callback_right = [=] (uint32_t) { add_direction(1, 0); };
        callback_up = [=] (uint32_t) { add_direction(0, -1); };
        callback_down = [=] (uint32_t) { add_direction(0, 1); };

        callback_win_left = [=] (uint32_t) { add_direction(-1, 0, output->get_top_view()); };
        callback_win_right = [=] (uint32_t) { add_direction(1, 0, output->get_top_view()); };
        callback_win_up = [=] (uint32_t) { add_direction(0, -1, output->get_top_view()); };
        callback_win_down = [=] (uint32_t) { add_direction(0, 1, output->get_top_view()); };

        auto section   = config->get_section("vswitch");
        auto key_left  = section->get_option("binding_left",  "<super> KEY_LEFT");
        auto key_right = section->get_option("binding_right", "<super> KEY_RIGHT");
        auto key_up    = section->get_option("binding_up",    "<super> KEY_UP");
        auto key_down  = section->get_option("binding_down",  "<super> KEY_DOWN");

        auto key_win_left  = section->get_option("binding_win_left",  "<super> <shift> KEY_LEFT");
        auto key_win_right = section->get_option("binding_win_right", "<super> <shift> KEY_RIGHT");
        auto key_win_up    = section->get_option("binding_win_up",    "<super> <shift> KEY_UP");
        auto key_win_down  = section->get_option("binding_win_down",  "<super> <shift> KEY_DOWN");

        if (key_left->as_key().valid())
            output->add_key(key_left,  &callback_left);
        if (key_right->as_key().valid())
            output->add_key(key_right, &callback_right);
        if (key_up->as_key().valid())
            output->add_key(key_up, &callback_up);
        if (key_down->as_key().valid())
            output->add_key(key_down, &callback_down);

        if (key_win_left->as_key().valid())
            output->add_key(key_win_left, &callback_win_left);
        if (key_win_right->as_key().valid())
            output->add_key(key_win_right, &callback_win_right);
        if (key_win_up->as_key().valid())
            output->add_key(key_win_up, &callback_win_up);
        if (key_win_down->as_key().valid())
            output->add_key(key_win_down, &callback_win_down);

        wayfire_touch_gesture activation_gesture;
        activation_gesture.finger_count = 4;
        activation_gesture.type = GESTURE_SWIPE;

        gesture_cb = [=] (wayfire_touch_gesture *gesture) {
            if (gesture->direction & GESTURE_DIRECTION_UP)
                add_direction(0, 1);
            if (gesture->direction & GESTURE_DIRECTION_DOWN)
                add_direction(0, -1);
            if (gesture->direction & GESTURE_DIRECTION_LEFT)
                add_direction(1, 0);
            if (gesture->direction & GESTURE_DIRECTION_RIGHT)
                add_direction(-1, 0);
        };
        output->add_gesture(activation_gesture, &gesture_cb);

        animation_duration = section->get_option("duration", "180");
        duration = wf_duration(animation_duration);
        hook = std::bind(std::mem_fn(&vswitch::slide_update), this);
    }

    void add_direction(int dx, int dy, wayfire_view view = nullptr) {
        if (!running)
            dirs.push({0, 0, view});

        if (dirs.size() < MAX_DIRS_IN_QUEUE)
            dirs.push({dx, dy, view});

        /* this is the first direction, we have pushed {0, 0} so that slide_done()
         * will do nothing on the first time */
        if (!running && start_switch())
            slide_done();
    }

    float sx, sy, tx, ty;

    struct animating_view {
        wayfire_view v;
        int ox, oy;
    };
    std::vector<animating_view> views;

    void slide_update()
    {
        float dx = duration.progress(sx, tx);
        float dy = duration.progress(sy, ty);
        for (auto v : views)
            v.v->move(v.ox + dx, v.oy + dy);

        if (!duration.running())
            slide_done();
    }

    void slide_done()
    {
        auto front = dirs.front();
        dirs.pop();

        GetTuple(vx, vy, output->workspace->get_current_workspace());
        auto old_ws = output->workspace->get_current_workspace();
        int dx = front.dx, dy = front.dy;

        vx += dx;
        vy += dy;

        for (auto v : views)
        {
            v.v->move(v.ox, v.oy);
            v.v->set_moving(false);
        }

        output->workspace->set_workspace(std::make_tuple(vx, vy));

        auto output_g = output->get_relative_geometry();
        if (front.view)
        {
            front.view->move(front.view->get_wm_geometry().x + dx * output_g.width,
                             front.view->get_wm_geometry().y + dy * output_g.height);
            output->focus_view(front.view);

            view_change_viewport_signal data;
            data.view = front.view;
            data.from = old_ws;
            data.to = output->workspace->get_current_workspace();

            output->emit_signal("view-change-viewport", &data);
        }

        views.clear();

        if (dirs.size() == 0) {
            stop_switch();
            return;
        }

        duration.start();
        dx = dirs.front().dx, dy = dirs.front().dy;
        wayfire_view static_view = front.view;

        GetTuple(sw, sh, output->get_screen_size());
        sx = sy = 0;
        tx = -dx * sw;
        ty = -dy * sh;

        GetTuple(vwidth, vheight, output->workspace->get_workspace_grid_size());
        if (vx + dx < 0 || vx + dx >= vwidth || vy + dy < 0 || vy + dy >= vheight) {
            stop_switch();
            return;
        }

        auto current_views = output->workspace->get_views_on_workspace(
                output->workspace->get_current_workspace(), WF_WM_LAYERS);
        auto next_views =
            output->workspace->get_views_on_workspace(
                std::make_tuple(vx + dx, vy + dy),
                WF_WM_LAYERS);

        std::set<wayfire_view> views_to_move;

        for (auto view : current_views)
            views_to_move.insert(view);
        for (auto view : next_views)
            views_to_move.insert(view);

        for (auto view : views_to_move) {
            if (view->is_mapped() && !view->destroyed && view != static_view)
            {
                view->set_moving(true);
                views.push_back({view, view->get_wm_geometry().x, view->get_wm_geometry().y});
            }
        }

        /* both workspaces are empty, so no animation, just switch */
        if (views_to_move.empty())
            slide_done();
    }

    bool start_switch()
    {
        if (!output->activate_plugin(grab_interface)) {
            dirs = std::queue<switch_direction> ();
            return false;
        }

        running = true;
        output->render->add_effect(&hook, WF_OUTPUT_EFFECT_PRE);
        output->render->auto_redraw(true);

        return true;
    }

    void stop_switch()
    {
        output->deactivate_plugin(grab_interface);
        dirs = std::queue<switch_direction> ();
        running = false;
        output->render->rem_effect(&hook, WF_OUTPUT_EFFECT_PRE);
        output->render->auto_redraw(false);
    }

    void fini()
    {
        if (running)
            stop_switch();

        output->rem_key(&callback_left);
        output->rem_key(&callback_right);
        output->rem_key(&callback_up);
        output->rem_key(&callback_down);

        output->rem_key(&callback_win_left);
        output->rem_key(&callback_win_right);
        output->rem_key(&callback_win_up);
        output->rem_key(&callback_win_down);

        output->rem_gesture(&gesture_cb);
    }
};

extern "C"
{
    wayfire_plugin_t* newInstance()
    {
        return new vswitch();
    }
}
