/* 
// YEET SHEET. By Luke Perkin. luke@locogame.co.uk
// Header comment last edited 21st 03/2021
// 
// "yeet" regions of text into a seperate buffer called a "yeet sheet". 
// The yeet sheet will be kept in sync with any text changes. 
// This allows you to have multiple portals into various files in your codebase.
// 
// == IMPLEMENTATION ==
// @include "4coder_loco_yeets.cpp" in your custom layer. 
//
// > loco_render_buffer(Application_Links *app, View_ID view_id, Face_ID face_id, Buffer_ID buffer, Text_Layout_ID text_layout_id, Rect_f32 rect, Frame_Info frame_info)
// Call this is your custom layer's "render" hook.
//
// > loco_on_buffer_edit(Application_Links *app, Buffer_ID buffer_id, Range_i64 old_range, Range_i64 new_range)
// Call this in your custom layer's "on buffer edit" hook.
//
// > loco_yeet_on_buffer_end(Application_Links *app, Buffer_ID buffer_id)
// Call this in your custom layer's "on buffer end" hook.
//
// == COMMANDS ==
// The main command you will want to bind to a key is the yeet range command:
// > loco_yeet_selected_range_or_jump
// That will yeet the selected range, or if the cursor is already inside a yeet range, jump to the location
// in the yeet buffer, or vice versa.
// 
// > loco_yeet_surrounding_function
// Selects the surrounding function then yeets it.
//
// > loco_yeet_tag
// Queries user for a string then searches all open buffers for that string
// as a comment tag (i.e. "// @tag") and yeets the scope it precedes.
// 
// > loco_yeet_clear
// Clears all current yeets.
//
// > loco_yeet_reset_all
// Clears all yeets in all slots, also remove the markers from the original buffers.
//
// > loco_yeet_remove_marker_pair
// Removes a single 'yeet', whatever one the cursor is currently inside.
//
// > loco_save_snapshot_1
// > loco_save_snapshot_2
// > loco_save_snapshot_3
// Saves the current collection of yeets to a slot.
 // Note that these do not persist if you close the editor.
// 
// > loco_load_yeet_snapshot_1
// > loco_load_yeet_snapshot_2
// > loco_load_yeet_snapshot_3
// Loads a yeet snapshot into the yeet buffer.
//
// > loco_jump_between_yeet
// Will attempt to jump to the corresponding location in the linked buffer.
//
// == CONFIG ==
// There are currently a few global variables below, their variable names are self-explanatory.
//
*/
CUSTOM_ID(attachment, loco_marker_handle);
CUSTOM_ID(attachment, loco_marker_pair_handle);

//--TYPES

// @yeettype
struct Loco_Marker_Pair
{
    i32 start_marker_idx;
    i32 end_marker_idx;
    i32 yeet_start_marker_idx;
    i32 yeet_end_marker_idx;
    Buffer_ID buffer;
};

// @yeettype
struct Loco_Yeets
{
    Loco_Marker_Pair pairs[1024];
    i32 pairs_count;
};

// @yeettype
struct Loco_Yeets_Snapshots
{
    Loco_Yeets snapshots[3];
    i32 snapshots_count;
};

//--GLOBALS

global bool loco_yeet_make_yeet_buffer_active_on_yeet = false;
global bool loco_yeet_show_highlight_ranges = true;
global bool loco_yeet_show_source_comment = true;
global FColor loco_yeet_source_comment_color = fcolor_change_alpha(f_green, 0.35f);
global FColor loco_yeet_highlight_start_color = fcolor_argb(0.f, 1.f, 0.f, 0.06f);
global FColor loco_yeet_highlight_end_color = fcolor_argb(0.f, 0.f, 1.f, 0.05f);

global Loco_Yeets_Snapshots yeets_snapshots = {};

// This is set to true when editing either buffer so you don't get
// infinite echoes of syncronization.
global bool lock_yeet_buffer = false;

// Set this to true to also delete the markers in the original buffer.
// I've left this at false because we might want to store different
// 'collections' of yeeted functions, switch between them would
// be as simple as saving a snapshot of the Loco_Yeets structure.
global bool loco_yeets_delete_og_markers = false;

//--IMPLEMENTATIONS

//~
static Buffer_ID
loco_get_yeet_buffer(Application_Links *app)
{
    String_Const_u8 yeet_name = string_u8_litexpr("*yeet*");
    Buffer_ID yeet_buffer = get_buffer_by_name(app, yeet_name, Access_Always);
    if (!buffer_exists(app, yeet_buffer))
    {
        yeet_buffer = create_buffer(app, yeet_name, BufferCreate_AlwaysNew);
        buffer_set_setting(app, yeet_buffer, BufferSetting_Unimportant, true);
    }
    return yeet_buffer;
}

//~ @marker @buffer
static Marker*
loco_get_buffer_markers(Application_Links *app, Arena *arena, Buffer_ID buffer_id, i32* count)
{
    // Adds a certain thing.
    Managed_Scope scope = buffer_get_managed_scope(app, buffer_id);
    Managed_Object* markers_obj = scope_attachment(app, scope, loco_marker_handle, Managed_Object);
    *count = managed_object_get_item_count(app, *markers_obj);
    Marker* markers = push_array(arena, Marker, *count);
    managed_object_load_data(app, *markers_obj, 0, *count, markers);
    return markers;
}

//~ @marker @buffer @overwrite
static void
loco_overwrite_buffer_markers(Application_Links *app, Arena *arena, Buffer_ID buffer_id, Marker* markers, i32 count)
{
    Managed_Scope scope = buffer_get_managed_scope(app, buffer_id);
    Managed_Object* markers_obj = scope_attachment(app, scope, loco_marker_handle, Managed_Object);
    managed_object_free(app, *markers_obj);
    *markers_obj = alloc_buffer_markers_on_buffer(
                                                  app,
                                                  buffer_id,
                                                  count,
                                                  &scope
                                                  );
    managed_object_store_data(app, *markers_obj, 0, count, markers);
}

//~ @overwrite
static void
loco_overwrite_yeets(Application_Links *app, Buffer_ID yeet_buffer, Loco_Yeets* yeets)
{
    Managed_Scope yeet_scope = buffer_get_managed_scope(app, yeet_buffer);
    Managed_Object* pair_obj = scope_attachment(app, yeet_scope, loco_marker_pair_handle, Managed_Object);
    
    i32 count = managed_object_get_item_count(app, *pair_obj);
    if (count == 0)
    {
        *pair_obj = alloc_managed_memory_in_scope(
                                                  app, yeet_scope, sizeof(Loco_Yeets), 1);
    }
    
    managed_object_store_data(app, *pair_obj, 0, 1, yeets);
}

//~ @buffer
static Loco_Yeets
loco_get_buffer_yeets(Application_Links *app, Buffer_ID buffer_id)
{
    Managed_Scope scope = buffer_get_managed_scope(app, buffer_id);
    Managed_Object* man_obj = scope_attachment(app, scope, loco_marker_pair_handle, Managed_Object);
    Loco_Yeets yeets = {};
    managed_object_load_data(app, *man_obj, 0, 1, &yeets);
    return yeets;
}

//~ @marker @append
// Append an array of markers to the buffer's managed objects.
static i32
loco_append_markers(Application_Links *app, Buffer_ID buffer_id, Marker* new_markers, i32 count)
{
    Managed_Scope scope;
    scope = buffer_get_managed_scope(app, buffer_id);
    Scratch_Block scratch(app);
    
    Temp_Memory marker_temp = begin_temp(scratch);
    Managed_Object* markers_obj = scope_attachment(app, scope, loco_marker_handle, Managed_Object);
    i32 marker_count = managed_object_get_item_count(app, *markers_obj);
    Marker* old_markers = push_array(scratch, Marker, marker_count);
    
    managed_object_load_data(app, *markers_obj, 0, marker_count, old_markers);
    managed_object_free(app, *markers_obj);
    
    *markers_obj = alloc_buffer_markers_on_buffer(
                                                  app,
                                                  buffer_id,
                                                  marker_count + count,
                                                  &scope
                                                  );
    managed_object_store_data(app, *markers_obj, 0, marker_count, old_markers);
    managed_object_store_data(app, *markers_obj, marker_count, count, new_markers);
    end_temp(marker_temp);
    
    return marker_count;
}

//~ @marker @range
// Pass in a buffer and a range of indices into a Marker array, this will retrieve the Markers
// array from its scope. Use loco_make_range_from_markers if you already have the Markers.
static Range_i64
loco_get_marker_range(Application_Links *app, Buffer_ID buffer_id, i32 start_idx, i32 end_idx)
{
    Scratch_Block scratch(app);
    i32 count = 0;
    Marker* markers = loco_get_buffer_markers(app, scratch, buffer_id, &count);
    if (markers != 0)
    {
        i64 start = markers[start_idx].pos;
        i64 end = markers[end_idx].pos;
        return Ii64(start, end);
    }
    return Ii64(0, 0);
}

//~ @marker @range
// Pass in an array of Markers and a range of indices to that array and this outputs
// a Range of character positions.
static Range_i64
loco_make_range_from_markers(Marker* markers, i32 start_idx, i32 end_idx)
{
    i64 start = markers[start_idx].pos;
    i64 end = markers[end_idx].pos;
    return Ii64(start, end);
}

//~
static void
loco_delete_marker_pair(Application_Links *app, Buffer_ID yeet_buffer, Loco_Yeets *yeets, i32 i)
{
    Scratch_Block scratch(app);
    
    // Swap delete pairs.
    Loco_Marker_Pair pair = yeets->pairs[i];
    yeets->pairs[i] = yeets->pairs[yeets->pairs_count-1];
    yeets->pairs_count -= 1;
    Loco_Marker_Pair &new_pair = yeets->pairs[i];
    
    // Need to swap out the indices too
    // becuase we've swapped deleted the markers so their index has changed.
    if (loco_yeets_delete_og_markers && new_pair.buffer == pair.buffer)
    {
        new_pair.start_marker_idx = pair.start_marker_idx;
        new_pair.end_marker_idx = pair.end_marker_idx;
    }
    // Always need to set the yeet_marker indices.
    new_pair.yeet_start_marker_idx = pair.yeet_start_marker_idx;
    new_pair.yeet_end_marker_idx = pair.yeet_end_marker_idx;
    
    // Swap delete yeet markers.
    i32 yeet_marker_count = 0;
    Marker* yeet_markers = loco_get_buffer_markers(app, scratch, yeet_buffer, &yeet_marker_count);
    yeet_markers[pair.yeet_start_marker_idx] = yeet_markers[yeet_marker_count - 2];
    yeet_markers[pair.yeet_end_marker_idx] = yeet_markers[yeet_marker_count - 1];
    yeet_marker_count -= 2;
    
    // Swap delete og markers.
    if (loco_yeets_delete_og_markers)
    {
        i32 og_marker_count = 0;
        Marker* og_markers = loco_get_buffer_markers(app, scratch, pair.buffer, &og_marker_count);
        og_markers[pair.start_marker_idx] = og_markers[og_marker_count - 2];
        og_markers[pair.end_marker_idx] = og_markers[og_marker_count - 1];
        og_marker_count -= 2;
        loco_overwrite_buffer_markers(app, scratch, pair.buffer, og_markers, og_marker_count);
    }
    
    // Store markers in memory.
    loco_overwrite_buffer_markers(app, scratch, yeet_buffer, yeet_markers, yeet_marker_count);
    loco_overwrite_yeets(app, yeet_buffer, yeets);
    
    Range_i64 yeet_range = loco_make_range_from_markers(yeet_markers, pair.yeet_start_marker_idx, pair.yeet_end_marker_idx);
    
    String_Const_u8 empty_str = string_u8_litexpr("");
    buffer_replace_range(app, yeet_buffer, yeet_range, empty_str);
}

//~ @buffer @edit
static void
loco_on_yeet_buffer_edit(Application_Links *app, Buffer_ID buffer_id, Range_i64 old_range, Range_i64 new_range)
{
    i64 insert_size = range_size(new_range);
    i64 text_shift = replace_range_shift(old_range, insert_size);
    u8 insert_char = buffer_get_char(app, buffer_id, old_range.min);
    
    Scratch_Block scratch(app);
    Loco_Yeets yeets = loco_get_buffer_yeets(app, buffer_id);
    i32 yeet_markers_count = 0;
    Marker* yeet_markers = loco_get_buffer_markers(app, scratch, buffer_id, &yeet_markers_count);
    for (size_t i = 0; i < yeets.pairs_count; i++)
    {
        Loco_Marker_Pair& pair = yeets.pairs[i];
        if (!buffer_exists(app, pair.buffer)) continue;
        
        Range_i64 yeet_range = loco_make_range_from_markers(yeet_markers, pair.yeet_start_marker_idx, pair.yeet_end_marker_idx);
        if (old_range.min > yeet_range.min && new_range.max < yeet_range.max)
        {
            // User edited inside a yeet block.
            Scratch_Block og_scratch(app);
            i32 og_markers_count = 0;
            Marker* og_markers = loco_get_buffer_markers(app, og_scratch, pair.buffer, &og_markers_count);
            Range_i64 og_range = loco_make_range_from_markers(og_markers, pair.start_marker_idx, pair.end_marker_idx);
            String_Const_u8 string = push_buffer_range(app, og_scratch, buffer_id, yeet_range);
            buffer_replace_range(
                                 app, 
                                 pair.buffer,
                                 og_range,
                                 string
                                 );
        }
    }
}

//~ @buffer @edit
static void
loco_on_original_buffer_edit(Application_Links *app, Buffer_ID buffer_id, Range_i64 old_range, Range_i64 new_range)
{
    i64 insert_size = range_size(new_range);
    i64 text_shift = replace_range_shift(old_range, insert_size);
    u8 insert_char = buffer_get_char(app, buffer_id, old_range.min);
    
    // Get the yeet buffer.
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    if (!buffer_exists(app, yeet_buffer) || buffer_id == yeet_buffer) return;
    
    // Check if this buffer exists in the yeet list.
    Loco_Yeets yeets = loco_get_buffer_yeets(app, yeet_buffer);
    bool exists_in_yeets = false;
    for (i32 i = 0; i < yeets.pairs_count; i++)
    {
        if (yeets.pairs[i].buffer == buffer_id)
        {
            exists_in_yeets = true;
            break;
        }
    }
    if (!exists_in_yeets) return;
    
    // Get yeet list and markers for both buffers.
    Scratch_Block scratch(app);
    i32 yeet_markers_count = 0;
    Marker* yeet_markers = loco_get_buffer_markers(app, scratch, yeet_buffer, &yeet_markers_count);
    i32 og_markers_count = 0;
    Marker* og_markers = loco_get_buffer_markers(app, scratch, buffer_id, &og_markers_count);
    for (i32 i = 0; i < yeets.pairs_count; i++)
    {
        Loco_Marker_Pair pair = yeets.pairs[i];
        if (pair.buffer != buffer_id) continue;
        Range_i64 og_range = loco_make_range_from_markers(
                                                          og_markers, pair.start_marker_idx, pair.end_marker_idx);
        if (old_range.min > og_range.min && new_range.max < og_range.max)
        {
            // User edited inside an original buffer block.
            Range_i64 yeet_range = loco_make_range_from_markers(
                                                                yeet_markers, pair.yeet_start_marker_idx, pair.yeet_end_marker_idx);
            String_Const_u8 string = push_buffer_range(app, scratch, buffer_id, og_range);
            buffer_replace_range(
                                 app, 
                                 yeet_buffer,
                                 yeet_range,
                                 string
                                 );
        }
    }
}

//~ @api @buffer @edit
api(LOCO) void 
loco_on_buffer_edit(Application_Links *app, Buffer_ID buffer_id, Range_i64 old_range, Range_i64 new_range)
{
    Buffer_ID yeet_buffer = get_buffer_by_name(app, string_u8_litexpr("*yeet*"), Access_Always);
    if (buffer_id == yeet_buffer)
    {
        if (!lock_yeet_buffer)
        {
            lock_yeet_buffer = true;
            loco_on_yeet_buffer_edit(app, buffer_id, old_range, new_range);
            lock_yeet_buffer = false;
        }
    }
    else if (!lock_yeet_buffer)
    {
        lock_yeet_buffer = true;
        loco_on_original_buffer_edit(app, buffer_id, old_range, new_range);
        lock_yeet_buffer = false;
    }
}

//~ @api @buffer @render
api(LOCO) void
loco_render_buffer(Application_Links *app, View_ID view_id, Face_ID face_id,
                   Buffer_ID buffer, Text_Layout_ID text_layout_id,
                   Rect_f32 rect, Frame_Info frame_info)
{
    String_Const_u8 name = string_u8_litexpr("*yeet*");
    Buffer_ID yeet_buffer = get_buffer_by_name(app, name, Access_Always);
    if (!buffer_exists(app, yeet_buffer)) return;
    Loco_Yeets yeets = loco_get_buffer_yeets(app, yeet_buffer);
    
    if (buffer == yeet_buffer && loco_yeet_show_source_comment)
    {
        Scratch_Block scratch(app);
        i32 markers_count = 0;
        Marker* markers = loco_get_buffer_markers(app, scratch, yeet_buffer, &markers_count);
        f32 line_height = get_view_line_height(app, view_id);
        FColor comment_color = loco_yeet_source_comment_color;
        for (i32 i = 0; i < yeets.pairs_count; i++)
        {
            Loco_Marker_Pair pair = yeets.pairs[i];
            if (!buffer_exists(app, pair.buffer)) continue;
            
            Range_i64 og_range = loco_get_marker_range(
                                                       app, pair.buffer, pair.start_marker_idx, pair.end_marker_idx);
            i64 start_line = get_line_number_from_pos(app, pair.buffer, og_range.min);
            i64 end_line = get_line_number_from_pos(app, pair.buffer, og_range.max);
            Fancy_Line line = {};
            String_Const_u8 unique_name = push_buffer_unique_name(app, scratch, pair.buffer);
            push_fancy_string(scratch, &line, fcolor_zero(), unique_name);
            push_fancy_stringf(scratch, &line, fcolor_zero(), " - Lines: %3.lld - %3.lld", start_line, end_line);
            i64 start_pos = markers[pair.yeet_start_marker_idx].pos;
            Rect_f32 start_rect = text_layout_character_on_screen(app, text_layout_id, start_pos);
            Vec2_f32 comment_pos = { start_rect.x0 + 0, start_rect.y0 - line_height };
            draw_fancy_line(app, face_id, comment_color, &line, comment_pos);
        }
    }
    
    if (loco_yeet_show_highlight_ranges)
    {
        FColor start_color = loco_yeet_highlight_start_color;
        FColor end_color = loco_yeet_highlight_end_color;
        for (i32 i = 0; i < yeets.pairs_count; i++)
        {
            Loco_Marker_Pair pair = yeets.pairs[i];
            Range_i64 range = {};
            if (pair.buffer == buffer || buffer == yeet_buffer)
            {
                if (buffer == yeet_buffer)
                {
                    range = loco_get_marker_range(app, buffer, pair.yeet_start_marker_idx, pair.yeet_end_marker_idx);
                }
                else
                {
                    range = loco_get_marker_range(app, buffer, pair.start_marker_idx, pair.end_marker_idx);
                }
                i64 start_line_number = get_line_number_from_pos(app, buffer, range.min);
                draw_line_highlight(
                                    app, 
                                    text_layout_id, 
                                    start_line_number, 
                                    start_color);
                i64 end_line_number = get_line_number_from_pos(app, buffer, range.max);
                draw_line_highlight(
                                    app, 
                                    text_layout_id, 
                                    end_line_number, 
                                    end_color);
            }
        }
    }
}

//~ @api @buffer
api(LOCO) void
loco_on_buffer_end(Application_Links *app, Buffer_ID buffer_id)
{
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    Loco_Yeets yeets = loco_get_buffer_yeets(app, yeet_buffer);
    for (i32 i = yeets.pairs_count - 1; i >= 0; i--)
    {
        if (yeets.pairs[i].buffer == buffer_id)
        {
            loco_delete_marker_pair(app, yeet_buffer, &yeets, i);
        }
    }
}

//~
static bool
loco_is_cursor_inside_yeet(Application_Links *app, i64 cursor_pos, i64 *out_dst_cursor_pos, Buffer_ID *out_dst_buffer)
{
    View_ID view = get_active_view(app, Access_Always);
    Buffer_ID buffer = view_get_buffer(app, view, Access_Always);
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    bool is_yeet_buffer = (buffer == yeet_buffer);
    Loco_Yeets yeets = loco_get_buffer_yeets(app, yeet_buffer);
    
    for (i32 i = 0; i < yeets.pairs_count; i++)
    {
        Loco_Marker_Pair pair = yeets.pairs[i];
        Buffer_ID src_buf = is_yeet_buffer ? yeet_buffer : pair.buffer;
        i32 src_start = is_yeet_buffer ? pair.yeet_start_marker_idx : pair.start_marker_idx;
        i32 src_end = is_yeet_buffer ? pair.yeet_end_marker_idx : pair.end_marker_idx;
        Range_i64 src_range = loco_get_marker_range(app, src_buf, src_start, src_end);
        bool is_cursor_inside = (cursor_pos >= src_range.min && cursor_pos <= src_range.max);
        if (is_cursor_inside)
        {
            Buffer_ID dst_buf = !is_yeet_buffer ? yeet_buffer : pair.buffer;
            i32 dst_start = !is_yeet_buffer ? pair.yeet_start_marker_idx : pair.start_marker_idx;
            i32 dst_end = !is_yeet_buffer ? pair.yeet_end_marker_idx : pair.end_marker_idx;
            Range_i64 dst_range = loco_get_marker_range(app, dst_buf, dst_start, dst_end);
            if (out_dst_cursor_pos != 0)
            {
                *out_dst_cursor_pos = dst_range.min + (cursor_pos - src_range.min);
            }
            if (out_dst_buffer != 0)
            {
                *out_dst_buffer = dst_buf;
            }
            return true;
        }
    }
    
    return false;
}

//~ @jump @buffer
static void
loco_jump_to_buffer(Application_Links *app, Buffer_ID dst_buffer, i64 dst_cursor)
{
    View_ID view = get_next_view_after_active(app, Access_Always);
    view_set_buffer(app, view, dst_buffer, 0);
    view_set_active(app, view);
    view_set_cursor_and_preferred_x(app, view, seek_pos(dst_cursor));
    if (auto_center_after_jumps)
    {
        center_view(app);
    }
}

//~ @jump
static bool
loco_try_jump_between_yeet_pair(Application_Links *app)
{
    View_ID view = get_active_view(app, Access_Always);
    Buffer_ID buffer = view_get_buffer(app, view, Access_Always);
    i64 cursor_pos = view_get_cursor_pos(app, view);
    i64 dst_cursor_pos = 0;
    Buffer_ID dst_buffer = 0;
    bool success = loco_is_cursor_inside_yeet(app, cursor_pos, &dst_cursor_pos, &dst_buffer);
    if (success)
    {
        loco_jump_to_buffer(app, dst_buffer, dst_cursor_pos);
    }
    return success;
}

//~ @buffer
// Copies text region from one buffer to another buffer (appends to end)
// Returns the insertion region in the dest buffer.
static Range_i64
loco_copy_buffer_text_to_buffer(Application_Links *app, 
                                Arena * arena, 
                                Buffer_ID src_buffer, 
                                Buffer_ID dst_buffer, 
                                Range_i64 src_range)
{
    // Copy range string from original buffer.
    String_Const_u8 copy_string = push_buffer_range(app, arena, src_buffer, src_range);
    
    // Find dest buffer pos to start insertion.
    i64 dst_insert_start = (i64)buffer_get_size(app, dst_buffer);
    
    // Insert range to yeet buffer.
    lock_yeet_buffer = true;
    Buffer_Insertion insert = begin_buffer_insertion_at_buffered(app, dst_buffer, dst_insert_start, arena, KB(16));
    insertc(&insert, '\n');
    insert_string(&insert, copy_string);
    insertc(&insert, '\n');
    insertc(&insert, '\n');
    end_buffer_insertion(&insert);
    lock_yeet_buffer = false;
    
    // Find dest end buffer pos.
    i64 dst_insert_end = (i64)buffer_get_size(app, dst_buffer);
    
    // +1 to ignore start newline.
    // -2 to ignore the two end newlines.
    return Ii64(dst_insert_start + 1, dst_insert_end - 2);
}

//~ @marker @append
// Append two markers that sit and the start and end of a range.
static i32
loco_append_marker_range(Application_Links *app, Buffer_ID buffer, Range_i64 range)
{
    Marker markers[2];
    markers[0].pos = range.min; 
    markers[0].lean_right = false;
    markers[1].pos = range.max;
    markers[1].lean_right = true;
    return loco_append_markers(app, buffer, markers, 2);
}

//~ @snapshot
static void
loco_save_yeet_snapshot_to_slot(Application_Links *app, i32 slot)
{
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    Loco_Yeets yeets = loco_get_buffer_yeets(app, yeet_buffer);
    yeets_snapshots.snapshots[slot] = yeets;
}

//~ @snapshot
static void
loco_load_yeet_snapshot_from_slot(Application_Links *app, i32 slot)
{
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    loco_yeet_clear(app);
    Loco_Yeets unsorted_yeets = yeets_snapshots.snapshots[slot];
    
    Scratch_Block scratch(app);
    
    // Sort the pairs by the yeet_buffer marker index.
    Sort_Pair_i32* yeet_sorter = push_array(scratch, Sort_Pair_i32, unsorted_yeets.pairs_count);
    for (i32 i = 0; i < unsorted_yeets.pairs_count; i += 1){
        yeet_sorter[i].index = i;
        yeet_sorter[i].key = unsorted_yeets.pairs[i].yeet_start_marker_idx;
    }
    sort_pairs_by_key(yeet_sorter, unsorted_yeets.pairs_count);
    
    Loco_Yeets yeets = {};
    yeets.pairs_count = unsorted_yeets.pairs_count;
    for (i32 i = 0; i < unsorted_yeets.pairs_count; i += 1) {
        yeets.pairs[i] = unsorted_yeets.pairs[yeet_sorter[i].index];
    }
    
    // Delete all mentions of buffers that no longer exist.
    for (i32 i = yeets.pairs_count - 1; i >= 0; i--)
    {
        if (!buffer_exists(app, yeets.pairs[i].buffer))
        {
            // Swap delete.
            yeets.pairs[i] = yeets.pairs[yeets.pairs_count-1];
            yeets.pairs_count -= 1;
        }
    }
    
    // Insert the text into the yeet_buffer.
    for (i32 i = 0; i < yeets.pairs_count; i++)
    {
        Loco_Marker_Pair pair = yeets.pairs[i];
        Range_i64 og_range = loco_get_marker_range(app, pair.buffer, pair.start_marker_idx, pair.end_marker_idx);
        Range_i64 insertion_range = loco_copy_buffer_text_to_buffer(app, scratch, pair.buffer, yeet_buffer, og_range);
        loco_append_marker_range(app, yeet_buffer, insertion_range);
    }
    
    loco_overwrite_yeets(app, yeet_buffer, &yeets);
    
    // Show the yeet buffer in opposite view if not in yeet view already.
    View_ID view = get_active_view(app, Access_Always);
    Buffer_ID buffer = view_get_buffer(app, view, Access_Always);
    if (buffer != yeet_buffer)
    {
        View_ID yeet_view = get_next_view_after_active(app, Access_Always);
        view_set_buffer(app, yeet_view, yeet_buffer, 0);
        view_set_cursor_and_preferred_x(app, yeet_view, seek_pos(0));
    }
}

//~ @buffer
static void
loco_yeet_buffer_range(Application_Links *app, Buffer_ID buffer, Range_i64 range)
{
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    if (buffer == yeet_buffer || loco_is_cursor_inside_yeet(app, range.min, 0, 0))
    {
        return;
    }
    
    i32 old_marker_idx = loco_append_marker_range(app, buffer, range);
    
    // Copy range string from original buffer.
    Scratch_Block scratch(app);
    
    Range_i64 insertion_range = loco_copy_buffer_text_to_buffer(app, scratch, buffer, yeet_buffer, range);
    
    i32 old_yeet_marker_idx = loco_append_marker_range(app, yeet_buffer, insertion_range);
    
    // Show the yeet buffer in opposite view.
    View_ID yeet_view = get_next_view_after_active(app, Access_Always);
    view_set_buffer(app, yeet_view, yeet_buffer, 0);
    view_set_cursor_and_preferred_x(app, yeet_view, seek_pos(insertion_range.min));
    if (loco_yeet_make_yeet_buffer_active_on_yeet)
    {
        view_set_active(app, yeet_view);
    }
    
    // add marker pair to yeet table.
    Managed_Scope yeet_scope = buffer_get_managed_scope(app, yeet_buffer);
    Managed_Object* pair_obj = scope_attachment(app, yeet_scope, loco_marker_pair_handle, Managed_Object);
    Loco_Yeets yeets = {};
    if (!managed_object_load_data(app, *pair_obj, 0, 1, &yeets))
    {
        yeets.pairs_count = 0;
        *pair_obj = alloc_managed_memory_in_scope(app, yeet_scope, sizeof(Loco_Yeets), 1);
    }
    Loco_Marker_Pair& pair = yeets.pairs[yeets.pairs_count++];
    pair.buffer = buffer;
    pair.start_marker_idx = old_marker_idx;
    pair.end_marker_idx = old_marker_idx + 1;
    pair.yeet_start_marker_idx = old_yeet_marker_idx;
    pair.yeet_end_marker_idx = old_yeet_marker_idx + 1;
    managed_object_store_data(app, *pair_obj, 0, 1, &yeets);
}

//~ @command
CUSTOM_COMMAND_SIG(loco_jump_between_yeet)
CUSTOM_DOC("Jumps from the yeet sheet to the original buffer or vice versa.")
{
    loco_try_jump_between_yeet_pair(app);
}

//~ @command
CUSTOM_COMMAND_SIG(loco_yeet_selected_range_or_jump)
CUSTOM_DOC("Yeets some code into a yeet buffer.")
{
    View_ID view = get_active_view(app, Access_Always);
    Buffer_ID buffer = view_get_buffer(app, view, Access_Always);
    Range_i64 range = get_view_range(app, view);
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    
    // The "or jump" part.
    if (loco_try_jump_between_yeet_pair(app))
        return;
    
    loco_yeet_buffer_range(app, buffer, range);
}

//~ @command
CUSTOM_COMMAND_SIG(loco_yeet_surrounding_function)
CUSTOM_DOC("Selects the surrounding function scope and yeets it.")
{ 
    View_ID view = get_active_view(app, Access_ReadVisible);
    Buffer_ID buffer = view_get_buffer(app, view, Access_ReadVisible);
    // Select the surrounding {} braces.
    i64 pos = view_get_cursor_pos(app, view);
    Range_i64 range = {};
    if (find_surrounding_nest(app, buffer, pos, FindNest_Scope, &range)){
        for (;;){
            pos = range.min;
            if (!find_surrounding_nest(app, buffer, pos, FindNest_Scope, &range)){
                break;
            }
        }
        i64 start_line = get_line_number_from_pos(app, buffer, range.min);
        start_line -= 2;
        if (start_line < 1) start_line = 1;
        range = Ii64(get_line_start_pos(app, buffer, start_line), range.max);
        select_scope(app, view, range);
    }
    // yeet it.
    loco_yeet_selected_range_or_jump(app);
}

//~ @command
CUSTOM_COMMAND_SIG(loco_yeet_clear)
CUSTOM_DOC("Clears all yeets.")
{
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    
    Loco_Yeets yeets = loco_get_buffer_yeets(app, yeet_buffer);
    if (loco_yeets_delete_og_markers)
    {
        for (i32 i = 0; i < yeets.pairs_count; i++)
        {
            Managed_Scope scope = buffer_get_managed_scope(app, yeets.pairs[i].buffer);
            Managed_Object* markers_obj = scope_attachment(
                                                           app, scope, loco_marker_handle, Managed_Object);
            managed_object_free(app, *markers_obj);
        }
    }
    
    {
        Managed_Scope scope = buffer_get_managed_scope(app, yeet_buffer);
        Managed_Object* markers_obj = scope_attachment(app, scope, loco_marker_handle, Managed_Object);
        managed_object_free(app, *markers_obj);
        Managed_Object* pair_obj = scope_attachment(app, scope, loco_marker_pair_handle, Managed_Object);
        managed_object_free(app, *pair_obj);
    }
    
    clear_buffer(app, yeet_buffer);
}

//~ @command
CUSTOM_COMMAND_SIG(loco_yeet_reset_all)
CUSTOM_DOC("Clears all yeets in all snapshots, also clears all the markers.")
{
    bool cache_delete_og_markers = loco_yeets_delete_og_markers;
    loco_yeets_delete_og_markers = true;
    loco_load_yeet_snapshot_from_slot(app, 0);
    loco_yeet_clear(app);
    loco_load_yeet_snapshot_from_slot(app, 1);
    loco_yeet_clear(app);
    loco_load_yeet_snapshot_from_slot(app, 2);
    loco_yeet_clear(app);
    loco_yeets_delete_og_markers = cache_delete_og_markers;
    loco_load_yeet_snapshot_from_slot(app, 0);
    yeets_snapshots = {};
}

//~ @command
CUSTOM_COMMAND_SIG(loco_yeet_remove_marker_pair)
CUSTOM_DOC("Removes the marker pair the cursor is currently inside.")
{
    
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    View_ID view = get_active_view(app, Access_Always);
    Buffer_ID buffer = view_get_buffer(app, view, Access_Always);
    i64 cursor_pos = view_get_cursor_pos(app, view);
    if (!loco_is_cursor_inside_yeet(app, cursor_pos, 0, 0)) return;
    
    // If we're in the original buffer,
    // try to jump to the relative location in the yeet before.
    // That way I just keep the one branch of logic for deleting
    // from the yeet buffer only.
    View_ID cached_view = view;
    if (buffer != yeet_buffer)
    {
        loco_try_jump_between_yeet_pair(app);
        view = get_active_view(app, Access_Always);
        buffer = view_get_buffer(app, view, Access_Always);
    }
    
    if (buffer == yeet_buffer)
    {
        Scratch_Block scratch(app);
        Range_i64 range = get_view_range(app, view);
        Loco_Yeets yeets = loco_get_buffer_yeets(app, yeet_buffer);
        i32 yeet_marker_count = 0;
        Marker* yeet_markers = loco_get_buffer_markers(app, scratch, yeet_buffer, &yeet_marker_count);
        for (i32 i = yeets.pairs_count - 1; i >= 0; i--)
        {
            Loco_Marker_Pair pair = yeets.pairs[i];
            Range_i64 yeet_range = loco_make_range_from_markers(
                                                                yeet_markers, pair.yeet_start_marker_idx, pair.yeet_end_marker_idx);
            if (range.max > yeet_range.min && range.max < yeet_range.max)
            {
                loco_delete_marker_pair(app, yeet_buffer, &yeets, i);
                break;
            }
        }
        
        view_set_active(app, cached_view);
    }
}

//--SNAPSHOTS

//~ @command @snapshot
CUSTOM_COMMAND_SIG(loco_save_yeet_snapshot_1)
CUSTOM_DOC("Save yeets snapshot to slot 1.")
{
    loco_save_yeet_snapshot_to_slot(app, 0);
}

//~ @command @snapshot
CUSTOM_COMMAND_SIG(loco_save_yeet_snapshot_2)
CUSTOM_DOC("Save yeets snapshot to slot 2.")
{
    loco_save_yeet_snapshot_to_slot(app, 1);
}

//~ @command @snapshot
CUSTOM_COMMAND_SIG(loco_save_yeet_snapshot_3)
CUSTOM_DOC("Save yeets snapshot to slot 3.")
{
    loco_save_yeet_snapshot_to_slot(app, 2);
}

//~ @command @snapshot
CUSTOM_COMMAND_SIG(loco_load_yeet_snapshot_1)
CUSTOM_DOC("Load yeets snapshot from slot 1.")
{
    loco_load_yeet_snapshot_from_slot(app, 0);
}

//~ @command @snapshot
CUSTOM_COMMAND_SIG(loco_load_yeet_snapshot_2)
CUSTOM_DOC("Load yeets snapshot from slot 2.")
{
    loco_load_yeet_snapshot_from_slot(app, 1);
}

//~ @command @snapshot
CUSTOM_COMMAND_SIG(loco_load_yeet_snapshot_3)
CUSTOM_DOC("Load yeets snapshot from slot 3.")
{
    loco_load_yeet_snapshot_from_slot(app, 2);
}

//--CATEGORIES

// @yeettags @yeettype
enum Loco_Yeet_Tags_Parse_State
{
    Loco_Tag_PState_Looking_For_Tag,
    Loco_Tag_PState_Reading_Word,
    Loco_Tag_PState_End_Of_Word,
    Loco_Tag_PState_Looking_For_Comment,
    Loco_Tag_PState_Looking_For_Scope_Start,
    Loco_Tag_PState_Looking_For_Scope_End
};

// @yeettags @yeettype
struct Loco_Yeet_Tag_Range
{
    Range_i64 range;
};

//~ @yeettags
static void
loco_yeet_all_scopes_with_tag(Application_Links *app, Buffer_ID buffer, String_Const_u8 tag_name)
{
    Token_Array token_arr = get_token_array_from_buffer(app, buffer);
    if (token_arr.tokens == 0) return;
    
    Scratch_Block scratch(app);
    Token_Iterator_Array it = token_iterator_index(buffer, &token_arr, 0);
    Loco_Yeet_Tag_Range tag_ranges[1024];
    u64 tag_ranges_count = 0;
    Loco_Yeet_Tags_Parse_State root_parse_state = Loco_Tag_PState_Looking_For_Comment;
    u64 scope_enter_count = 0;
    
    for(Token* tok = token_it_read(&it);
        tok != 0;
        tok = token_it_read(&it))
    {
        if (HasFlag(tok->flags, TokenBaseFlag_PreprocessorBody))
        {
            if (!token_it_inc_non_whitespace(&it)) break;
            continue;
        }
        
        if (tok->sub_kind == TokenCppKind_LineComment && root_parse_state == Loco_Tag_PState_Looking_For_Comment)
        {
            String_Const_u8 tok_str = push_buffer_range(app, scratch, buffer, Ii64(tok));
            Loco_Yeet_Tags_Parse_State parse_state = Loco_Tag_PState_Looking_For_Tag;
            i64 tag_start = 0;
            i64 tag_end = 0;
            bool found_tag = false;
            
            for (u64 i = 0; i <= tok_str.size; i++)
            {
                u8 c = (i < tok_str.size) ? tok_str.str[i] : 0;
                switch(parse_state)
                {
                    case Loco_Tag_PState_Looking_For_Tag: {
                        if (c == '@')
                        {
                            parse_state = Loco_Tag_PState_Reading_Word;
                            tag_start = i+1;
                        }
                        break;
                    }
                    case Loco_Tag_PState_Reading_Word: {
                        bool is_lower_alpha = (c >= 'a' && c <= 'z');
                        bool is_upper_alpha = (c >= 'A' && c <= 'Z');
                        bool is_numeric = (c >= '0' && c <= '9');
                        bool is_alphanumeric = (is_lower_alpha || is_upper_alpha || is_numeric);
                        if (!is_alphanumeric)
                        {
                            parse_state = Loco_Tag_PState_End_Of_Word;
                            tag_end = i;
                        }
                        break;
                    }
                }
                
                if (parse_state == Loco_Tag_PState_End_Of_Word)
                {
                    parse_state = Loco_Tag_PState_Looking_For_Tag;
                    String_Const_u8 sub = string_substring(tok_str, Ii64(tag_start, tag_end));
                    if (string_match(sub, tag_name))
                    {
                        found_tag = true;
                        break;
                    }
                }
            }
            
            if (found_tag)
            {
                root_parse_state = Loco_Tag_PState_Looking_For_Scope_Start;
                tag_ranges[tag_ranges_count].range.min = tok->pos;
            }
        }
        
        if (tok->sub_kind == TokenCppKind_BraceOp)
        {
            if (root_parse_state == Loco_Tag_PState_Looking_For_Scope_Start)
            {
                scope_enter_count = 1;
                root_parse_state = Loco_Tag_PState_Looking_For_Scope_End;
            }
            else if (root_parse_state == Loco_Tag_PState_Looking_For_Scope_End)
            {
                scope_enter_count += 1;
            }
        }
        
        if (tok->sub_kind == TokenCppKind_BraceCl && root_parse_state == Loco_Tag_PState_Looking_For_Scope_End)
        {
            scope_enter_count -= 1;
            if (scope_enter_count == 0)
            {
                root_parse_state = Loco_Tag_PState_Looking_For_Comment;
                tag_ranges[tag_ranges_count].range.max = tok->pos+1;
                tag_ranges_count += 1;
            }
        }
        
        if (!token_it_inc_non_whitespace(&it)) break;
    }
    
    for (u64 i = 0; i < tag_ranges_count; i++)
    {
        loco_yeet_buffer_range(app, buffer, tag_ranges[i].range);
    }
}

//--TAG-COMMANDS

// @command @yeettags
CUSTOM_COMMAND_SIG(loco_yeet_tag)
CUSTOM_DOC("Find all locations of a comment tag (//@tag) in all buffers and yeet the scope they precede.")
{
    Scratch_Block scratch(app);
    u8 *space = push_array(scratch, u8, KB(1));
    String_Const_u8 tag_name = get_query_string(app, "Yeet Tag: ", space, KB(1));
    Buffer_ID yeet_buffer = loco_get_yeet_buffer(app);
    for (Buffer_ID buffer = get_buffer_next(app, 0, Access_ReadWriteVisible);
         buffer != 0;
         buffer = get_buffer_next(app, buffer, Access_ReadWriteVisible))
    {
        if (buffer == yeet_buffer) continue;
        
        loco_yeet_all_scopes_with_tag(app, buffer, tag_name);
    }
}


