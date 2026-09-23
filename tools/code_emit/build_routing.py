"""Turn the ticket 21.5 routing decisions into tools/code_emit/data/routing.json.

Routing assigns every pool_fdps function and every game-owned global to a
target .c/.h before ticket 22 and 23 start emitting, so that no emit round has
to decide where its output goes.  The decisions live here in three tables:
CHAPTER_SPLITS holds the chapter-numbered handler families, RULES matches the
other families that were decided as families, and OVERRIDES names a single
function whose file was decided against what its name would suggest.  Anything
matching none of them is an error, not a default -- a silent fallback bucket is
how a routing table stops describing the program.

The line estimate is the Ghidra decompiled line count from
workspace/code_emit/routing_inputs/functions.json, which
tools/code_emit/DumpRoutingInputs.java produces.  It is an estimate: real emit
output diverges, and rebuild_info/code_layout.md owns what to do when it does.

Globals are routed by use, not by name: a symbol read from exactly one target
file belongs to that file, and one read from several belongs to gamedata.c,
whose whole job is to own the state no single subsystem owns.

Usage:
    python tools/code_emit/build_routing.py            # write routing.json
    python tools/code_emit/build_routing.py --stats    # per-file totals only
    python tools/code_emit/build_routing.py --check    # verify, write nothing
"""

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ROOT / "workspace" / "code_emit" / "routing_inputs"
DATA = ROOT / "tools" / "code_emit" / "data"

# A target .c whose projected line count exceeds this is split into cohesive
# sub-files.  The number is a working limit, not a property of the program: it
# is the point past which a model re-reads far more of a file than it changes.
LINE_BUDGET = 1000

# Watcom's DOS-hosted tools have no long filename support, so a basename over
# eight characters is silently truncated into a different file.
MAX_BASENAME = 8

# Shared globals live here.  Not a leftovers bucket: the entry condition is
# "read from more than one target file", which is a fact about the program.
SHARED_DATA_TARGET = "gamedata.c"

# DGROUP boundaries, owned by program_info/memory_layout.md: _edata and _end as
# the CRT startup loads them.  They decide how much C a global turns into --
# below _edata a symbol carries an initialiser, above it it is one tentative
# definition -- so the estimate needs them, not just the symbol's size.
DATA_END = 0x63930   # _edata
BSS_END = 0x6a3bc    # _end
CODE_BLOCK_END = 0x57c60   # end of .object1, where read-only tables sit

# How many initialiser elements are written per source line.  A guess, and
# deliberately a coarse one: the point is to catch a table that would land a
# five-thousand-line file, not to predict formatting.
INIT_ELEMENTS_PER_LINE = 8


def chapter_event_file(n):
    """Which chevtN.c a chapter's scripted event handlers land in.

    The boundaries are cut so that no file passes the line budget, which is
    why they are uneven: chapter 8's four handlers are worth as much as
    chapters 9 to 14 put together.

    That last sentence is also where the chevt2 / chevt2b boundary is.  Both
    halves of chapters 8 to 14 were routed to one file, which reached 1106
    emitted lines with chapter 8's fourth handler still to come, so chapter 8
    -- the half that is one chapter's own script, four handlers off one map
    file -- keeps chevt2.c and chapters 9 to 14 went to chevt2b.c.

    chevt5b.c is the same cut one family along: the four handlers of chapters
    24 and 25 alone came out at 1113 emitted lines, so chapters 26 and 27 were
    cut off into a file of their own rather than let chapter 27's handler land
    on top of that.  Neither new file is chevt7.c, because chevt6.c is already
    chapters 28 to 30: the letter keeps the family in chapter order.
    """
    if n <= 7:
        return "chevt1.c"
    if n <= 8:
        return "chevt2.c"
    if n <= 14:
        return "chevt2b.c"
    if n <= 19:
        return "chevt3.c"
    if n <= 23:
        return "chevt4.c"
    if n <= 25:
        return "chevt5.c"
    if n <= 27:
        return "chevt5b.c"
    return "chevt6.c"


def chapter_init_file(n):
    """Which chinitN.c a chapter's entry handler lands in.

    chinit1b.c is the chevt2b.c cut made in the init family.  Chapters 1 to 15
    were one file, which reached 1024 emitted lines with chapter 10's handler
    -- five handlers still to come -- because every handler carries the map
    file's player-slot count, the cut-scene's own deployments and the roster
    arithmetic that decide whether its four or five calls do anything, and that
    is a page of comment per handler whatever the body costs.  So chapters 1 to
    10 keep chinit1.c and chapters 11 to 15 were cut off into a file of their
    own before the eleventh landed on top of them.

    It is not chinit3.c, because chinit2.c was already chapters 16 to 30: the
    letter keeps the family in chapter order, as it does for chevt2b.c and
    chevt5b.c.

    chinit2b.c is that same cut made a second time, for the same reason.
    Chapters 16 to 30 were one file, which reached 1108 emitted lines with
    chapter 24's handler and six more still to come.  Chapter 24 is where the
    cut falls because it ends the run of chapters the party is assembled over:
    蘭斯洛特 joins at 19 and 珊 at 24, and after that no entry handler adds
    anybody, so chapters 16 to 24 are the handlers whose page of comment is
    roster arithmetic -- who is on the roster, how many player slots the map
    asks for, whether the add lands inside the array -- and chapters 25 to 30
    are the run-in to the ending, all six of them the same plain four-call body
    differing only in which cut-scene member they name.
    """
    if n <= 10:
        return "chinit1.c"
    if n <= 15:
        return "chinit1b.c"
    if n <= 24:
        return "chinit2.c"
    return "chinit2b.c"


def chapter_end_file(n):
    """Which chendN.c a chapter's end handler lands in.

    chend1b.c is the chinit1b.c cut made in the end family.  Chapters 1 to 15
    were one file, which reached 1021 emitted lines with chapter 11's handler
    and four still to come.  The bodies are four or five calls each, but every
    one of them carries a page saying whether its chapter can be reached with
    enemies still standing -- which is what decides whether the map sweep that
    opens the handler is the load-bearing step or a belt-and-braces one -- and
    that page costs the same whatever the body costs.

    The cut falls after chapter 11 because the four still to come are a segment
    of their own: chapters 12 to 14 are the last three copies of the family's
    template, sweep the map then bank the party, play the scene, revive the
    fallen and store the next index, and chapter 15 is the segment's exception,
    the only handler of chapters 3 to 15 that does not sweep the map and
    instead puts back on the field the party members the fortress-cannon duel
    retired.  Chapters 1 to 11 stay where they are, a shade over the estimate
    with nothing further routed to them: they are code_layout.md's second
    rule, a file over the estimate with no function left queued behind it, and
    moving reviewed handlers and the fixture-sharing tests that cover them into
    another file to buy twenty lines is what that rule says not to do.

    It is not chend3.c, because chend2.c is already chapters 16 to 30: the
    letter keeps the family in chapter order, as it does for chevt2b.c,
    chevt5b.c, chinit1b.c and chinit2b.c.

    chend2b.c is that same cut made a second time, after the chapter the
    chinit2b.c and chpost3.c cuts fall after.  Chapters 16 to 30 were one file,
    which reached 1015 emitted lines with chapter 24's handler and six more
    still to come.  Chapters 25 to 30 are the run-in to the ending and the only
    handlers in the family that can end the game: chapter 27's gates the hidden
    chapters on two carried items and, without them, plays the ending sequence
    and raises the return-to-title request instead of storing a next index;
    chapter 30's does the same unconditionally, because there is no chapter 31.
    Chapters 16 to 24 are the ones that only ever hand the game on, and the cut
    keeps their one pair together: chapters 19 and 24 are the second and third
    妖刀 duels, the only two of chapters 16 to 30 that put the party back on
    its maxima before banking it, and they share that recovery's shape and its
    note.  The nine already emitted stay where they are, so the cut moves no
    code.
    """
    if n <= 11:
        return "chend1.c"
    if n <= 15:
        return "chend1b.c"
    if n <= 24:
        return "chend2.c"
    return "chend2b.c"


def chapter_post_action_file(n):
    """Which chpostN.c a chapter's post-action handler lands in.

    chpost3.c is the chinit2b.c cut made in the post-action family, and it
    falls after the same chapter for a related reason.  Chapters 16 to 30 were
    one file, which reached 1160 emitted lines with chapters 23 and 24 -- one
    of them the longest handler of the fifteen -- still to come.

    The cut is after chapter 24 because that is where the map stops changing
    shape.  Every hard-coded unit index these handlers carry is measured from
    the map's player-slot count, header byte +1 of MAPnn.DAT: the unit array is
    built by laying the player slots down first and appending the deployment
    records behind them, so an enemy's slot is that count plus its record
    number.  The count reads 10 for MAP15 to MAP18, chapters 16 to 19; 11 for
    MAP19 to MAP23, chapters 20 to 24; and 12 for MAP24 to MAP29, chapters 25
    to 30, stepping as 蘭斯洛特 joins in chapter 19 and 珊 in chapter 24, after
    which no handler of any family adds anybody.  So chapters 25 to 30 are the
    six played on a settled twelve-slot map, which is what puts the 魔戰將軍 and
    the 魔導王 at slots 0x0c onwards, and the paragraph deriving that base is
    written out in three of the six.

    The cut also keeps this file's two pairs together.  Chapters 19 and 24 are
    the second and third 妖刀 duels -- the first is chapter 15's, in chpost1.c --
    and they are the only two handlers of chapters 16 to 30 that speak to the
    player and move an item, sharing the frame-buffer, panel-pen and
    message-colour constants chapter 19 already carries.  Chapters 22 and 23 are
    the two that declare no victory of their own and lose on 法蓮娜 at slot 3.
    A cut anywhere between chapter 19 and chapter 24 separates one of the two
    pairs.

    It is chpost3.c and not chpost2b.c because chpost2.c is this family's
    highest-numbered file: unlike chevt2b.c, chinit1b.c and chinit2b.c, there is
    no later-numbered sibling for a new file to jump over, so a plain number
    keeps the family in chapter order by itself.
    """
    if n <= 15:
        return "chpost1.c"
    if n <= 24:
        return "chpost2.c"
    return "chpost3.c"


# The chapter-numbered handler families.  Each is one family of thirty (or, for
# the event handlers, of however many chapters scripted one), split by chapter
# number alone because there is nothing else to group them by: no two chapters'
# handlers share code, and the numbering is the game's own ordering.
CHAPTER_SPLITS = [
    (r"^fdps_chapter_(\d\d)_init$", chapter_init_file),
    (r"^fdps_chapter_(\d\d)_post_action$", chapter_post_action_file),
    (r"^fdps_chapter_(\d\d)_end$", chapter_end_file),
    (r"^fdps_chapter_(\d\d)_event_", chapter_event_file),
    (r"^fdps_chapter_(\d\d)_revive_", chapter_event_file),
]

# Ordered.  First match wins, so a narrow pattern must precede the broad family
# it sits inside.  Every entry is a family decided as a family; a function that
# happens to match but was decided individually belongs in OVERRIDES, which is
# consulted first.
RULES = [
    # --------------------------------------------------------- chapter frame
    (r"^fdps_chapter_event_set_game_over$", "chevt1.c"),
    (r"^fdps_chapter_state_reset$", "chapter.c"),

    # ------------------------------------------------------------ cutscenes
    (r"^fdps_icon_script_", "icon.c"),

    # ------------------------------------------------------------- map AI
    (r"^fdps_map_actor_(behavior_step|take_best_action|move_toward_)", "mapai.c"),
    (r"^fdps_map_actor_score_", "aiscore.c"),
    (r"^fdps_score_targets_", "aiscore.c"),
    (r"^fdps_map_actor_(use_item|cast_chosen_spell|move_and_attack)$", "aiact.c"),
    (r"^fdps_collect_targets_", "aitarget.c"),
    (r"^fdps_check_can_counter_attack", "aitarget.c"),

    # ---------------------------------------------- movement grid and paths
    (r"^fdps_move_grid_", "movegrid.c"),
    (r"^fdps_move_path_trace$", "movegrid.c"),
    (r"^fdps_map_grid_", "movegrid.c"),
    (r"^fdps_battle_move_unit_toward$", "movegrid.c"),

    # -------------------------------------------------------- map cell data
    (r"^fdps_map_(load_tile_info|set_pending_tile_event|"
     r"apply_triggered_cell_changes)$", "maptile.c"),

    # ------------------------------------------------------- battle engine
    (r"^fdps_battle_(enemy_turn_phase|npc_turn_phase|unit_turn|"
     r"player_phase_loop|advance_turn|run_turn_events|"
     r"end_phase_if_all_units_done|mark_unit_done)$", "btlturn.c"),
    (r"^fdps_battle_(check_default_end_conditions|destroy_remaining_enemies|"
     r"count_remaining_units_on_side|show_win_fail_window)$", "btlend.c"),
    # The battle screen's menus were one file, which reached 1038 emitted
    # lines with the outer system menu still to come.  They split along the
    # line the two callers already draw: fdps_battle_player_phase_loop opens
    # the system menu, which is the pause menu -- objectives, save, load, quit
    # -- and carries the whole FDE.SAV image the save arm rebuilds, while
    # fdps_battle_unit_turn opens the action ring one acting unit at a time,
    # and the cell search is that ring's fourth command and has no other
    # caller.  So btlmenu.c keeps the system pair and btlact.c takes the
    # acting unit's commands.
    (r"^fdps_battle_(system_menu|system_submenu)$", "btlmenu.c"),
    (r"^fdps_battle_(action_menu|search_cell_at_cursor)$", "btlact.c"),
    (r"^fdps_battle_find_unit_", "unit.c"),

    # -------------------------------------------------------------- combat
    (r"^fdps_combat_(play_attack_exchange|compute_hit_outcome|"
     r"slide_in_attacker|slide_backdrops)$", "combat.c"),
    (r"^fdps_combat_play_blow$", "cmbblow.c"),
    (r"^fdps_combat_play_spell_on_targets$", "cmbspell.c"),

    # ---------------------------------------------------------------- unit
    (r"^fdps_unit_(is_retired|is_flying|mark_retired|recompute_combat_stats|"
     r"select_status_icon|face_target)$", "unit.c"),
    (r"^fdps_get_unit_record$", "unit.c"),
    (r"^fdps_unit_(find_equipped_slot|get_item_id|item_count|"
     r"item_select_window|item_select_loop|remove_item|add_item|equip_window|"
     r"can_equip_item|equip_slot|find_item_slot)$", "unititem.c"),
    (r"^fdps_unit_(apply_heal|restore_mp|apply_damage|inflict_random_ailments|"
     r"apply_status_effect|is_ailment_immune|award_exp_and_level_up|"
     r"collect_known_spells)$", "unitstat.c"),
    (r"^fdps_unit_(attack_target|resolve_attack_hit|rest)$", "unitatk.c"),
    (r"^fdps_unit_status_window_wait_input$", "statunit.c"),

    # ------------------------------------------------------- table records
    (r"^fdps_get_(character_base|growth|enemy|item|class|class_equip|spell|"
     r"spell_learn|promotion|roster)_record$", "table.c"),

    # -------------------------------------------------------------- spells
    (r"^fdps_(cast_spell_on_targets|spell_damage_unit|spell_heal_unit|"
     r"spell_deduct_mp_cost|play_spell_11_cutscene|"
     r"play_spell_palette_flash)$", "spell.c"),
    (r"^fdps_spell_list_", "spellmnu.c"),
    (r"^fdps_battle_spell_command$", "spellmnu.c"),

    # --------------------------------------------------------------- items
    (r"^fdps_apply_(item_effect|damage|heal)_to_targets$", "item.c"),
    (r"^fdps_battle_item_menu$", "item.c"),

    # ---------------------------------------------------------- death path
    (r"^fdps_collect_death_script", "death.c"),

    # ------------------------------------------------------- deployment
    (r"^fdps_deploy_", "deploy.c"),

    # --------------------------------------------------------------- menus
    (r"^fdps_menu_", "menu.c"),

    # -------------------------------------------------------------- roster
    (r"^fdps_roster_", "roster.c"),

    # --------------------------------------------------------- save / load
    # Three files, cut where the subsystem's own seams are: the two modal
    # screens and the slot cursor they share; the page they are drawn on; and
    # FDE.SAV itself.  save.c held all eight and ran past the line budget with
    # two still to emit (rebuild_info/code_layout.md).
    (r"^fdps_(save_game_screen|load_game_screen|save_slot_select_loop)$",
     "save.c"),
    (r"^fdps_(saveload_screen_build|draw_save_slot_panel)$", "savepnl.c"),
    (r"^fdps_(load_savegame|compute_save_checksum|xor_crypt_buffer)$",
     "savefile.c"),

    # ------------------------------------------------------------- village
    (r"^fdps_(run_village_phase|village_signboard_menu|"
     r"village_animate_walk_to_destination|village_animate_window_zoom)$",
     "village.c"),
    (r"^fdps_village_(select_member|member_status_loop|item_sell_loop|"
     r"item_transfer_loop|member_equip_loop|item_menu)$", "vilmenu.c"),
    # The five village buildings split at the bar's door.  vilshop.c keeps the
    # three screens whose command row dispatches into the party's shared
    # counters -- promote, buy, sell, hand over, equip, status.  The bar's row
    # is the save, load and quit-to-title commands instead, and it is the only
    # caller of the lucky draw it opens on the way in, so the two travel
    # together in vilbar.c (rebuild_info/code_layout.md).
    (r"^fdps_run_(church_screen|weapon_shop|secret_menu)$", "vilshop.c"),
    (r"^fdps_run_(bar_shop|bonus_lottery)$", "vilbar.c"),
    (r"^fdps_shop_(collect_stock_items|select_item|select_buy_target|"
     r"buy_loop)$", "shop.c"),
    (r"^fdps_shop_(draw_item_entry|render_buy_target_frame|"
     r"draw_member_entry)$", "shopdraw.c"),
    (r"^fdps_church_", "church.c"),

    # --------------------------------------------------------------- audio
    (r"^fdps_audio_", "audio.c"),
    (r"^fdps_wav_", "audio.c"),
    (r"^fdps_(sfx_play|play_sfx|timer_tick_handler)$", "audio.c"),

    # ------------------------------------------------------------------ CD
    (r"^fdps_cd_(alloc_dos_buffers|device_request|ioctl_output_command|"
     r"status_is_not_busy|read_media_change_status|set_door_lock|close_tray|"
     r"read_head_sector|read_audio_channel_info|set_audio_channel_control)$",
     "cd.c"),
    (r"^fdps_cdrom_(detect|read_device_status)$", "cd.c"),
    (r"^fdps_cd_(set_music_track|music_repeat_poll|verify_disc_and_play_track|"
     r"play_audio_range|stop_audio|resume_audio|seek|play_track|"
     r"play_track_range|play_whole_disc|resolve_track_range|audio_is_idle|"
     r"read_audio_position|read_q_channel)$", "cdaudio.c"),
    (r"^fdps_cd_(unpack_msf|msf_to_sector|sector_to_msf|get_track_length_"
     r"sectors|get_track_length_msf|get_disk_info_msf|track_is_audio)$",
     "cdtoc.c"),
    (r"^fdps_cdrom_read_(upc|disk_info|track_info)$", "cdtoc.c"),

    # --------------------------------------------------------------- files
    (r"^fdps_vfs_", "vfs.c"),
    (r"^fdps_saf_", "saf.c"),

    # ------------------------------------------------------------ graphics
    (r"^fdps_rle_blit_(passthrough|scaled|mirrored_horizontal|"
     r"mirrored_vertical)$", "rle.c"),
    (r"^fdps_rle_skip_row$", "rle.c"),
    (r"^fdps_rle_blit_rotated", "rlerot.c"),
    (r"^fdps_rle_blit_(remap_sprite_and_backdrop|with_palette_remap|recolor)$",
     "rlecolor.c"),
    (r"^fdps_rle_blit_(translucent|tint)", "rleblend.c"),
    (r"^fdps_(cel_blit_sprite|cel_expand_sheet_24x24|blit_unit_sprite|"
     r"blit_command_sprite|blit_cursor_tile|draw_tilemap_cell|"
     r"draw_tilemap_layer|draw_composite_sprite)$", "sprite.c"),
    (r"^fdps_blit_glyph_1bpp$", "text.c"),
    (r"^fdps_draw_(text|glyph|number)$", "text.c"),
    (r"^fdps_blit_", "blit.c"),
    (r"^fdps_transition_", "transit.c"),
    (r"^fdps_(set_palette_range|set_palette_range_on_retrace|"
     r"build_palette_tables|palette_find_nearest_color|pack_rgb)$", "palette.c"),
    (r"^fdps_get_rgb_", "palette.c"),
    (r"^fdps_cycle_(scene|ui)_palette$", "palcycle.c"),
    (r"^fdps_draw_(scene_layers|scene_layer|map_unit|map_units)$", "mapdraw.c"),
    (r"^fdps_(build_scene_layer_draw_order|render_view_frame)$", "mapdraw.c"),
    (r"^fdps_(draw_map_cursor|draw_cursor_info_panel)$", "mapcur.c"),
    (r"^fdps_map_cursor_", "mapcur.c"),

    # ------------------------------------------------------ gauges / panels
    (r"^fdps_draw_(gauge_bar|gauge_bar_proportional|gauge_fill|stat_gauge|"
     r"unit_hp_mp_gauges|unit_gauge|unit_gauge_proportional)$", "gauge.c"),
    (r"^fdps_battle_(show_combat_gauges|compute_unit_gauge_position)$",
     "gauge.c"),
    (r"^fdps_(load_status_cel_image|close_status_window|"
     r"draw_status_window_anim_frame)$", "statwin.c"),
    (r"^fdps_battle_show_unit_status_window$", "statwin.c"),
    (r"^fdps_draw_unit_(status_panel|inventory)$", "statunit.c"),
    (r"^fdps_message_window_", "msgwin.c"),

    # --------------------------------------------------- floating markers
    (r"^fdps_show_(number|miss|cure|sprite)_indicator$", "indicat.c"),
    (r"^fdps_play_indicator_queue$", "indicat.c"),
    (r"^fdps_flash_units_in_color$", "indicat.c"),

    # ----------------------------------------------------- walk animation
    (r"^fdps_(walk_step_down|animate_move_step_up|animate_move_step_left|"
     r"animate_move_step_right|animate_move_path)$", "walk.c"),

    # ------------------------------------------------------------ platform
    (r"^fdps_dpmi_", "dpmi.c"),
    (r"^fdps_(keyboard_scancode_ptr|keyboard_isr|wait_any_key|"
     r"flush_keyboard_queue|read_keyboard_queue|install_keyboard_isr|"
     r"uninstall_keyboard_isr|read_scancode_auto_repeat)$", "keybd.c"),

    # ------------------------------------------------------------- startup
    (r"^main$", "main.c"),
    (r"^fdps_(shutdown_free_resources|load_global_resources|load_data_tables|"
     r"free_global_resource_buffers)$", "main.c"),
    (r"^fdps_(title_screen|title_demo|show_game_over|play_movie)$", "title.c"),
]

# One function, one decision, one reason.  Everything here was routed against
# what its name alone would have suggested; the reasons are reproduced in
# rebuild_info/code_layout.md.
OVERRIDES = {
    "fdps_map_find_chest_cell": ("mapai.c",
        "Named for the map layer but it exists only to answer the AI's "
        "'where is the chest' question; its one caller is the AI step."),
    "fdps_draw_composite_sprite": ("sprite.c",
        "The generic sprite-request drawer every animation goes through, not "
        "map drawing; grouped with the other sprite primitives."),
    "fdps_render_ring_menu_frame": ("menu.c",
        "Renders the ring menu the menu loop drives; its callers are the "
        "menu open/close/input trio."),
    "fdps_load_and_draw_portrait": ("msgwin.c",
        "FACE.CEL portraits: two of its three callers are the message "
        "windows, which is where the portrait buffer is owned."),
    "fdps_prompt_two_choice": ("msgwin.c",
        "A modal window with its own input loop -- the same family as the "
        "message windows, not part of any one of its many callers."),
    "fdps_options_menu": ("menu.c",
        "Built out of the shared menu open/cursor/close primitives; a menu "
        "instance, not a settings subsystem."),
    "fdps_play_ending_credit_roll": ("ending.c",
        "The ending sequence is a file of its own: one animated epilogue card "
        "per roster member, reached from two chapter endings and owned by "
        "neither, and big enough that title.c cannot hold it and the title "
        "screen too."),
    "fdps_play_death_animation_and_mark_dead": ("death.c",
        "The death sequence and the scripts it fires are one pipeline."),
    "fdps_run_death_scripts": ("death.c",
        "Pays out and fires what fdps_collect_death_scripts gathered."),
    "fdps_collect_death_scripts": ("death.c",
        "Head of the death-script pipeline."),
    "fdps_level_up_apply_stat_gain": ("unitstat.c",
        "Its only caller is fdps_unit_award_exp_and_level_up and it writes "
        "unit stat fields."),
    "fdps_animate_turn_banner": ("anim.c",
        "A VFS/SAF animation player like the rest of anim.c, not part of the "
        "battle turn logic it announces."),
    "fdps_draw_turn_number": ("anim.c",
        "Draws into the turn banner's own sprite request, and nothing else."),
    "fdps_play_vfs_animation": ("anim.c",
        "Full-screen MISC.VFS animation playback."),
    "fdps_play_vfs_animation_over_units": ("anim.c",
        "The same playback, per unit cell."),
    "fdps_play_attack_animation": ("anim.c",
        "The on-map playback half of a blow; the numeric resolution it "
        "accompanies is combat.c's."),
    "fdps_baseani_get_entry_or_exit": ("anim.c",
        "Resident BaseAni.vfs accessor, used only by the banner animation."),
    "fdps_show_chapter_title_card": ("chapter.c",
        "Chapter-level presentation called by all thirty inits; it belongs to "
        "the chapter frame, not to any one chapter."),
    "fdps_field_load_chapter_resources": ("rsrc.c",
        "Chapter resource load-and-free, the same job as the field variant."),
    "fdps_load_field_chapter_resources": ("rsrc.c",
        "Chapter resource load-and-free for the village side."),
    "fdps_load_indexed_archive_entry": ("rsrc.c",
        "Generic indexed-archive reader with no caller, so grouped by job."),
    "fdps_cache_cel_sprite_group": ("rsrc.c",
        "The .CEL sprite cache, read by seven unrelated callers."),
    "fdps_build_map_unit_array": ("deploy.c",
        "Builds the unit array the deployment table fills; head of the "
        "deployment pipeline."),
    "fdps_draw_spell_list_page": ("spellmnu.c",
        "The spell list UI page, drawn only for the spell menus."),
    "fdps_set_flag_bit": ("unit.c",
        "Writes a per-unit flag bitfield through fdps_get_unit_record."),
    "fdps_object_set_field34_low_nibble_range": ("unit.c",
        "Writes unit record field 0x34 over an index range."),
    "fdps_units_clear_status_bit7": ("unit.c",
        "Sweeps the unit record array clearing one status bit."),
    "fdps_relocate_unit_array": ("unit.c",
        "Moves the unit record array; the owner of that array's storage."),
    "fdps_fill_screen_square": ("blit.c",
        "Paints a solid square straight into the mode 13h screen -- a blit "
        "primitive, not part of the overview screen that calls it."),
    "fdps_render_map_overview_scaled": ("overview.c",
        "The scaled map render exists only for the overview screen."),
    "fdps_battle_map_overview": ("overview.c",
        "The overview screen itself, split out of the battle engine with its "
        "renderer so the pair stays together."),
    "fdps_draw_party_gold": ("village.c",
        "The gold readout is drawn by seven village and shop screens and by "
        "nothing else."),
    "fdps_check_secret_code_key": ("village.c",
        "Feeds the signboard menu's secret unlock code."),
    "fdps_saf_play_over_background": ("saf.c",
        "Kept with the SAF frame reader rather than anim.c: it is the SAF "
        "player itself, which anim.c drives."),
    "fdps_battle_tick_status_effects": ("unitstat.c",
        "Ticks poison and the other per-unit status effects, so it belongs "
        "with the code that applies and clears them, not with the turn "
        "engine that calls it once a turn."),
    "fdps_saf_play_over_scene": ("saf.c",
        "The same player, compositing over the live scene."),
}


# Globals decided by hand, against what "who reads it" alone would have said.
DATA_OVERRIDES = {
    "data_fdps_chapter_init_handler_table": ("chapter.c",
        "One of the four chapter dispatch tables. Routed by readers they "
        "would scatter across three files while naming handlers in six "
        "others; the chapter frame is what they belong to."),
    "data_fdps_chapter_event_handler_table": ("chapter.c",
        "Same family: the fifty scripted event handlers' dispatch table."),
    "data_fdps_chapter_post_action_handler_table": ("chapter.c",
        "Same family: the thirty post-action handlers' dispatch table."),
    "data_fdps_chapter_end_handler_table": ("chapter.c",
        "Same family. Read from main.c alone, but splitting it away from its "
        "three siblings would hide that they are one dispatch surface."),
    "data_fdps_indicator_queue_count": ("indicat.c",
        "Layout neighbour: must follow data_fdps_indicator_queue_glyph_ids "
        "(indicat.c) contiguously so the unbounded glyph_ids overrun lands on "
        "this cursor as in the original."),
    "data_fdps_animation_baseani_entry_ptr": ("gamedata.c",
        "Layout neighbour: must sit at 0x643ec right after "
        "data_fdps_shared_quit_game_requested in gamedata.c's contiguous "
        "initialised run 0x64120..0x653ef, which the indicator-queue glyph "
        "overrun writes through."),
    "data_fdps_battle_indicator_queue_unit_idx": ("gamedata.c",
        "Layout neighbour: must follow data_fdps_indicator_queue_cell_x_offset "
        "and precede data_fdps_indicator_queue_glyph_ids inside gamedata.c's "
        "contiguous initialised run 0x64120..0x653ef, which the unbounded "
        "indicator-queue cursor overruns."),
    "data_fdps_indicator_queue_cell_x_offset": ("gamedata.c",
        "Layout neighbour: 4-aligned head of gamedata.c's contiguous "
        "initialised run 0x64120..0x653ef (29 globals, 25 already there), "
        "which the unbounded indicator-queue cursor overruns."),
}


# Symbols Ghidra had to name but the rebuild must not define.  Each is a thing
# the compiler puts back on its own from the C it is given, so defining it
# would produce a second copy under a name the original never had.  The key is
# the name prefix; the value is the pseudo-target recorded in routing.json and
# the reason it is not emitted.
SKIP_PATTERNS = [
    (r"^binary_artifact_string_literal_", "<inline:string-literal>",
     "A string literal, not a named global. It comes back as the literal in "
     "the statement that uses it; naming it would invent a symbol the "
     "original never had. See rebuild_info/naming.md."),
    (r"^binary_artifact_local_array_init_", "<inline:local-array-initialiser>",
     "The compiler's read-only copy of a function-local array initialiser. "
     "It comes back from declaring that local with its initialiser inside "
     "the owning function, which is where its contents belong."),
    (r"^binary_artifact_.*_switch_table_", "<compiler:switch-table>",
     "A jump table the compiler emits for a switch statement. It comes back "
     "from the switch itself; writing it out by hand would pin the rebuild "
     "to Ghidra's reading of the original codegen."),
    (r"^binary_artifact_fp_const_", "<compiler:fp-constant>",
     "A floating-point constant the compiler parked in the constant pool. "
     "It comes back from writing the literal in the expression that uses "
     "it."),
]


def skip_verdict(symbol):
    for pattern, target, reason in SKIP_PATTERNS:
        if re.search(pattern, symbol):
            return target, reason
    return None


def load_json(name):
    path = INPUTS / name
    if not path.exists():
        sys.exit("missing %s -- run DumpRoutingInputs.java first" % path)
    return json.loads(path.read_text(encoding="utf-8"))


def route(name):
    """Target .c for one function name, or None if no decision covers it."""
    if name in OVERRIDES:
        return OVERRIDES[name][0]
    for pattern, chooser in CHAPTER_SPLITS:
        m = re.search(pattern, name)
        if m:
            return chooser(int(m.group(1)))
    for pattern, target in RULES:
        if re.search(pattern, name):
            return target
    return None


def data_segment(addr_hex):
    """Which DGROUP segment an address falls in, for the line estimate."""
    off = int(addr_hex, 16)
    if off < CODE_BLOCK_END:
        return "rodata"
    if off < DATA_END:
        return "data"
    if off < BSS_END:
        return "bss"
    return "other"


def data_lines(segment, size, type_name):
    """Projected source lines for one global definition.

    A BSS symbol is a tentative definition: one line, whatever its size.  An
    initialised one carries its contents, so it grows with its element count.
    """
    if segment == "bss":
        return 1
    m = re.search(r"\[(\d+)\]", type_name or "")
    elements = int(m.group(1)) if m else max(1, size)
    return max(1, -(-elements // INIT_ELEMENTS_PER_LINE))


def route_globals(globals_rows, fn_target):
    """Assign each game-owned global to the file whose code reads it.

    Read from one target file -> that file owns it.  Read from several ->
    gamedata.c, because no single subsystem can own it without every other
    subsystem reaching across for it.  Read from none -> also gamedata.c: an
    unreferenced global still has to be defined somewhere for the image to
    match, and no subsystem has a claim on it.
    """
    out = {}
    skipped = {}
    unclassified = []
    for g in globals_rows:
        targets = set()
        foreign = set()
        readers = []
        for ref in g["referenced_by"]:
            m = re.match(r"^(\S+) \[(\w*)\] @([0-9a-f]{8})$", ref)
            if not m:
                continue
            name, pool, _addr = m.groups()
            if pool != "fdps":
                foreign.add(pool)
                continue
            readers.append(name)
            t = fn_target.get(name)
            if t:
                targets.add(t)

        verdict = skip_verdict(g["symbol"])
        if verdict:
            # Vendor-owned artefacts are ticket 14's business, not this one's.
            if not readers:
                continue
            target, reason = verdict
            skipped[g["addr"]] = {
                "symbol": g["symbol"],
                "target": target,
                "type": g["type"],
                "size": g["size"],
                "carried_by": sorted(set(readers)),
                "carried_in": sorted(targets),
                "reason": reason,
                "status": "skip",
            }
            continue

        if not g["symbol"].startswith("data_fdps_"):
            # A symbol game code reads that is neither game-owned nor a known
            # compiler artefact has no verdict, and silence would drop it from
            # both ticket 23's worklist and this table.
            # switchdataD_/caseD_/default are the decompiler's own switch
            # annotations sharing an address with the table itself, not
            # symbols anything has to define.
            if readers and not g["symbol"].startswith(
                    ("data_ail_", "L$", "s_", "fix_off32_", "switchD",
                     "switchdataD", "caseD", "default")):
                unclassified.append(
                    "%s %s: read by %s but neither game-owned nor a known "
                    "compiler artefact" % (g["addr"], g["symbol"],
                                           sorted(set(readers))[0]))
            continue
        if g["addr"] in out:
            # The table is keyed by address, so two game-owned symbols sharing
            # one would silently lose the second.
            unclassified.append("%s: two game-owned symbols share this "
                                "address (%s and %s)"
                                % (g["addr"], out[g["addr"]]["symbol"],
                                   g["symbol"]))
        if g["symbol"] in DATA_OVERRIDES:
            target, reason = DATA_OVERRIDES[g["symbol"]]
        elif len(targets) == 1:
            target = next(iter(targets))
            reason = "read only from %s" % target
        elif not targets:
            target = SHARED_DATA_TARGET
            reason = ("no pool_fdps reader" if not foreign
                      else "read only from %s code" % "/".join(sorted(foreign)))
        else:
            target = SHARED_DATA_TARGET
            reason = "read from %d files: %s" % (
                len(targets), ", ".join(sorted(targets)))
        segment = data_segment(g["addr"])
        out[g["addr"]] = {
            "symbol": g["symbol"],
            "target": target,
            "type": g["type"],
            "size": g["size"],
            "segment": segment,
            "est_lines": data_lines(segment, g["size"], g["type"]),
            "ref_count": g["ref_count"],
            "reader_files": sorted(targets),
            "reason": reason,
            "status": "pending",
        }
    return out, skipped, unclassified


def build():
    fns = load_json("functions.json")["functions"]
    globs = load_json("globals.json")["globals"]

    unrouted = []
    fn_target = {}
    entries = {}
    for f in fns:
        t = route(f["name"])
        if t is None:
            unrouted.append(f)
            continue
        fn_target[f["name"]] = t
        entry = {
            "name": f["name"],
            "target": t,
            "est_lines": f["dec_lines"],
            "body_size": f["size"],
            "status": "pending",
        }
        if f["name"] in OVERRIDES:
            entry["routing_note"] = OVERRIDES[f["name"]][1]
        entries[f["addr"]] = entry

    data_entries, skipped, unclassified = route_globals(globs, fn_target)
    return fns, entries, data_entries, skipped, unclassified, unrouted


def file_stats(entries, data_entries):
    """Per target file: function count, code lines, global count, data lines."""
    by = {}
    for e in entries.values():
        row = by.setdefault(e["target"], {"fn": 0, "code": 0, "data": 0, "gl": 0})
        row["fn"] += 1
        row["code"] += e["est_lines"]
    for d in data_entries.values():
        row = by.setdefault(d["target"], {"fn": 0, "code": 0, "data": 0, "gl": 0})
        row["gl"] += 1
        row["data"] += d["est_lines"]
    for row in by.values():
        row["total"] = row["code"] + row["data"]
    return by


def check(entries, data_entries, unclassified, unrouted):
    """Every invariant the routing table has to hold, as a list of failures."""
    problems = []
    if unrouted:
        problems.append("%d functions match no rule: %s" % (
            len(unrouted), ", ".join(f["name"] for f in unrouted[:5])))
    problems.extend(unclassified)

    by = file_stats(entries, data_entries)
    for target, row in sorted(by.items()):
        if row["total"] > LINE_BUDGET:
            problems.append("%s projects %d lines, over the %d budget"
                            % (target, row["total"], LINE_BUDGET))

    stems = set()
    for target in by:
        stem = target[:-2]
        if len(stem) > MAX_BASENAME:
            problems.append("%s: basename over %d characters"
                            % (target, MAX_BASENAME))
        lowered = stem.lower()
        if lowered in stems:
            problems.append("%s: basename collides case-insensitively" % target)
        stems.add(lowered)

    names = [e["name"] for e in entries.values()]
    if len(names) != len(set(names)):
        problems.append("a function name is routed twice")
    return problems


def write_markdown(entries, data_entries, skipped, by):
    """The human-readable face of routing.json, regenerated with it.

    Deliberately a listing and not an argument: why the grouping is what it is
    belongs to rebuild_info/code_layout.md, and a second copy of that reasoning
    here would be a second copy to keep true.
    """
    out = []
    out.append("# Emit routing table\n")
    out.append("Generated by `tools/code_emit/build_routing.py`; do not edit "
               "by hand. The decisions are the tables in that script, and "
               "`rebuild_info/code_layout.md` explains them. "
               "`tools/code_emit/data/routing.json` is the machine-readable "
               "form and the one the workflows read.\n")
    out.append("Line counts are Ghidra decompiled line counts, an estimate of "
               "how much C each function becomes. The budget is %d lines per "
               "file.\n" % LINE_BUDGET)

    out.append("\n## Files\n")
    out.append("| File | fn | code lines | globals | data lines | total |")
    out.append("| --- | ---: | ---: | ---: | ---: | ---: |")
    for target in sorted(by):
        r = by[target]
        out.append("| `%s` | %d | %d | %d | %d | %d |"
                   % (target, r["fn"], r["code"], r["gl"], r["data"],
                      r["total"]))
    totals = {k: sum(r[k] for r in by.values())
              for k in ("fn", "code", "gl", "data", "total")}
    out.append("| **total** | **%d** | **%d** | **%d** | **%d** | **%d** |"
               % (totals["fn"], totals["code"], totals["gl"], totals["data"],
                  totals["total"]))

    out.append("\n## Functions by file\n")
    by_target = {}
    for addr, e in entries.items():
        by_target.setdefault(e["target"], []).append((addr, e))
    for target in sorted(by_target):
        rows = sorted(by_target[target])
        out.append("\n### `%s` -- %d functions, %d lines\n"
                   % (target, len(rows), sum(e["est_lines"] for _, e in rows)))
        out.append("| address | lines | function |")
        out.append("| --- | ---: | --- |")
        for addr, e in rows:
            out.append("| `%s` | %d | `%s` |" % (addr, e["est_lines"], e["name"]))

    out.append("\n## Globals by file\n")
    by_data = {}
    for addr, d in data_entries.items():
        by_data.setdefault(d["target"], []).append((addr, d))
    for target in sorted(by_data):
        rows = sorted(by_data[target])
        out.append("\n### `%s` -- %d globals\n" % (target, len(rows)))
        out.append("| address | segment | symbol | why here |")
        out.append("| --- | --- | --- | --- |")
        for addr, d in rows:
            out.append("| `%s` | %s | `%s` | %s |"
                       % (addr, d["segment"], d["symbol"], d["reason"]))

    out.append("\n## Not emitted\n")
    out.append("Symbols Ghidra had to name that the rebuild must not define, "
               "because the compiler puts each one back from the C it is "
               "given. They are excluded from ticket 23's worklist.\n")
    kinds = {}
    for d in skipped.values():
        kinds.setdefault(d["target"], []).append(d)
    out.append("| kind | count | reason |")
    out.append("| --- | ---: | --- |")
    for kind in sorted(kinds):
        out.append("| `%s` | %d | %s |"
                   % (kind, len(kinds[kind]), kinds[kind][0]["reason"]))

    path = DATA / "routing.md"
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    return path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--check", action="store_true")
    args = ap.parse_args()

    fns, entries, data_entries, skipped, unclassified, unrouted = build()
    problems = check(entries, data_entries, unclassified, unrouted)

    by = file_stats(entries, data_entries)
    for target in sorted(by):
        row = by[target]
        print("%-12s %3d fn %5d + %3d gl %4d = %5d lines%s"
              % (target, row["fn"], row["code"], row["gl"], row["data"],
                 row["total"], "   OVER" if row["total"] > LINE_BUDGET else ""))
    print("---")
    print("functions   %d routed, %d unrouted" % (len(entries), len(unrouted)))
    print("targets     %d .c files" % len(by))
    print("est lines   %d" % sum(e["est_lines"] for e in entries.values()))
    print("globals     %d routed" % len(data_entries))
    shared = sum(1 for d in data_entries.values()
                 if d["target"] == SHARED_DATA_TARGET)
    print("            %d to %s, %d to a single owner"
          % (shared, SHARED_DATA_TARGET, len(data_entries) - shared))
    skip_kinds = {}
    for s in skipped.values():
        skip_kinds[s["target"]] = skip_kinds.get(s["target"], 0) + 1
    print("skipped     %d symbols" % len(skipped))
    for kind in sorted(skip_kinds):
        print("            %-34s %d" % (kind, skip_kinds[kind]))
    for f in unrouted:
        print("  UNROUTED %s %s" % (f["addr"], f["name"]))
    for p in problems:
        print("  PROBLEM  %s" % p)

    if args.stats or args.check:
        return 1 if problems else 0

    if problems:
        print("refusing to write routing.json while problems stand")
        return 1

    DATA.mkdir(parents=True, exist_ok=True)
    doc = ("Ticket 21.5's routing table: which .c every pool_fdps function and "
           "every game-owned global belongs in, decided before ticket 22 and 23 "
           "start emitting so that no emit round has to choose a file. Keys are "
           "entry-point addresses. `est_lines` is the Ghidra decompiled line "
           "count the file's size was budgeted from, not a promise about the "
           "emitted C. Regenerate with tools/code_emit/build_routing.py, whose "
           "tables hold the decisions; rebuild_info/code_layout.md explains "
           "them and owns the rule for what to do when a file outgrows its "
           "budget.")
    out = {
        "_doc": doc,
        "line_budget": LINE_BUDGET,
        "functions": dict(sorted(entries.items())),
        "globals": dict(sorted(data_entries.items())),
        "skipped": dict(sorted(skipped.items())),
    }
    path = DATA / "routing.json"
    path.write_text(json.dumps(out, indent=2, ensure_ascii=False) + "\n",
                    encoding="utf-8")
    print("wrote %s" % path)
    print("wrote %s" % write_markdown(entries, data_entries, skipped, by))
    return 0


if __name__ == "__main__":
    sys.exit(main())
