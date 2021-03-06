#include <output.hpp>
#include <core.hpp>
#include <view.hpp>
#include <workspace-manager.hpp>
#include <render-manager.hpp>
#include <linux/input.h>
#include <signal-definitions.hpp>
#include "snap_signal.hpp"
#include <libweston-desktop.h>
#include "../../shared/config.hpp"

class wayfire_move : public wayfire_plugin_t
{
    signal_callback_t move_request, view_destroyed;
    button_callback activate_binding;
    touch_callback touch_activate_binding;
    wayfire_view view;

    bool is_using_touch;
    bool enable_snap;
    int slot;
    int snap_pixels;

    int prev_x, prev_y;

    public:
        void init(wayfire_config *config)
        {
            grab_interface->name = "move";
            grab_interface->abilities_mask = WF_ABILITY_CHANGE_VIEW_GEOMETRY;

            auto section = config->get_section("move");
            wayfire_button button = section->get_button("activate", {MODIFIER_ALT, BTN_LEFT});
            if (button.button == 0)
                return;

            activate_binding = [=] (weston_pointer* ptr, uint32_t)
            {
                is_using_touch = false;
                auto view = core->find_view(ptr->focus);
                if (!view || view->is_special)
                    return;
                this->initiate(view, ptr->x, ptr->y);
            };

            touch_activate_binding = [=] (weston_touch* touch,
                    wl_fixed_t sx, wl_fixed_t sy)
            {
                is_using_touch = true;
                auto view = core->find_view(touch->focus);
                if (!view || view->is_special)
                    return;
                initiate(view, sx, sy);
            };

            output->add_button(button.mod, button.button, &activate_binding);
            output->add_touch(button.mod, &touch_activate_binding);

            enable_snap = section->get_int("enable_snap", 1);
            snap_pixels = section->get_int("snap_threshold", 2);

            using namespace std::placeholders;
            grab_interface->callbacks.pointer.button =  [=] (weston_pointer *ptr,
                    uint32_t b, uint32_t state)
            {
                if (b != button.button)
                    return;

                is_using_touch = false;
                input_pressed(state);
            };
            grab_interface->callbacks.pointer.motion = [=] (weston_pointer *ptr,
                    weston_pointer_motion_event*)
            {
                input_motion(ptr->x, ptr->y);
            };

            grab_interface->callbacks.touch.motion = [=] (weston_touch*,
                    int32_t id, wl_fixed_t sx, wl_fixed_t sy)
            {
                if (id > 0) return;
                input_motion(sx, sy);
            };

            grab_interface->callbacks.touch.up = [=] (weston_touch*, int32_t id)
            {
                if (id == 0)
                    input_pressed(WL_POINTER_BUTTON_STATE_RELEASED);
            };

            move_request = std::bind(std::mem_fn(&wayfire_move::move_requested), this, _1);
            output->connect_signal("move-request", &move_request);

            view_destroyed = [=] (signal_data* data)
            {
                auto conv = static_cast<destroy_view_signal*> (data);
                assert(conv);

                if (conv->destroyed_view == view)
                {
                    view = nullptr;
                    input_pressed(WL_POINTER_BUTTON_STATE_RELEASED);
                }
            };
            output->connect_signal("detach-view", &view_destroyed);
            output->connect_signal("destroy-view", &view_destroyed);
        }

        void move_requested(signal_data *data)
        {
            auto converted = static_cast<move_request_signal*> (data);

            if(converted && converted->view) {
                auto seat = core->get_current_seat();

                auto ptr = weston_seat_get_pointer(seat);
                auto touch = weston_seat_get_touch(seat);

                if (ptr && ptr->grab_serial == converted->serial) {
                    is_using_touch = false;
                    initiate(converted->view, ptr->x, ptr->y);
                } else if (touch && touch->grab_serial == converted->serial) {
                    is_using_touch = true;
                    initiate(converted->view, touch->grab_x, touch->grab_y);
                }
            }
        }

        void initiate(wayfire_view view, wl_fixed_t sx, wl_fixed_t sy)
        {
            if (view->destroyed)
                return;

            if (!output->workspace->
                    get_implementation(output->workspace->get_current_workspace())->
                        view_movable(view))
                return;

            if (!output->activate_plugin(grab_interface))
                return;

            weston_seat_break_desktop_grabs(core->get_current_seat());
            if (!grab_interface->grab()) {
                output->deactivate_plugin(grab_interface);
                return;
            }

            prev_x = wl_fixed_to_int(sx);
            prev_y = wl_fixed_to_int(sy);

            if (view->maximized)
                view->set_maximized(false);
            if (view->fullscreen)
                view->set_fullscreen(false);

            if (!view->is_special)
                view->output->focus_view(nullptr);
            view->set_moving(true);

            if (enable_snap)
                slot = 0;

            this->view = view;
            output->render->auto_redraw(true);
        }

        void input_pressed(uint32_t state)
        {
            if (state != WL_POINTER_BUTTON_STATE_RELEASED)
                return;

            grab_interface->ungrab();
            output->deactivate_plugin(grab_interface);
            output->render->auto_redraw(false);

            if (view)
            {
                if (view->is_special)
                    return;

                view->output->focus_view(view);
                view->set_moving(false);

                if (enable_snap && slot != 0) {
                    snap_signal data;
                    data.view = view;
                    data.tslot = (slot_type)slot;

                    output->emit_signal("view-snap", &data);
                }
            }
        }

        int calc_slot()
        {
            auto g = output->get_full_geometry();

            bool is_left = std::abs(prev_x - g.x) <= snap_pixels;
            bool is_right = std::abs(g.x + g.width - prev_x) <= snap_pixels;
            bool is_top = std::abs(prev_y - g.y) < snap_pixels;
            bool is_bottom = std::abs(g.y + g.height - prev_y) < snap_pixels;

            if (is_left && is_top)
                return SLOT_TL;
            else if (is_left && is_bottom)
                return SLOT_BL;
            else if (is_left)
                return SLOT_LEFT;
            else if (is_right && is_top)
                return SLOT_TR;
            else if (is_right && is_bottom)
                return SLOT_BR;
            else if (is_right)
                return SLOT_RIGHT;
            else if (is_top)
                return SLOT_CENTER;
            else if (is_bottom)
                return SLOT_BOTTOM;
            else
                return 0;
        }

        void input_motion(wl_fixed_t sx, wl_fixed_t sy)
        {
            int nx = wl_fixed_to_int(sx);
            int ny = wl_fixed_to_int(sy);

            view->move(view->geometry.x + nx - prev_x,
                    view->geometry.y + ny - prev_y);
            prev_x = nx;
            prev_y = ny;


            auto target_output = core->get_output_at(nx, ny);
            if (target_output != output)
            {
                weston_view_damage_below(view->handle);
                weston_view_geometry_dirty(view->handle);

                move_request_signal req;
                req.view = view;
                req.serial = is_using_touch ?  weston_seat_get_touch(core->get_current_seat())->grab_serial :
                    weston_seat_get_pointer(core->get_current_seat())->grab_serial;

                core->move_view_to_output(view, target_output);
                core->focus_output(target_output);
                target_output->emit_signal("move-request", &req);

                return;
            }

            /* TODO: possibly show some visual indication */
            if (enable_snap)
                slot = calc_slot();
        }
};

extern "C" {
    wayfire_plugin_t* newInstance()
    {
        return new wayfire_move();
    }
}

