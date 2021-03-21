# YEET SHEET.
By Luke Perkin. luke@locogame.co.uk

"yeet" regions of text into a seperate buffer called a "yeet sheet". 
The yeet sheet will be kept in sync with any text changes. 
This allows you to have multiple portals into various files in your codebase.

See video examples:  
https://www.youtube.com/watch?v=kMwC9wAFZKc  
https://www.youtube.com/watch?v=uXli6SCTsco

## IMPLEMENTATION
> `#include "4coder_loco_yeets.cpp"`
in your custom layer. 

> `loco_render_buffer(Application_Links *app, View_ID view_id, Face_ID face_id, Buffer_ID buffer, Text_Layout_ID text_layout_id, Rect_f32 rect, Frame_Info frame_info)`
Call this is your custom layer's "render" hook.

> `loco_on_buffer_edit(Application_Links *app, Buffer_ID buffer_id, Range_i64 old_range, Range_i64 new_range)`
Call this in your custom layer's "on buffer edit" hook.

> `loco_on_buffer_end(Application_Links *app, Buffer_ID buffer_id)`
Call this in your custom layer's "on buffer end" hook.

## COMMANDS
The main command you will want to bind to a key is the yeet range command:

> `loco_yeet_selected_range_or_jump`
That will yeet the selected range, or if the cursor is already inside a yeet range, jump to the location
in the yeet buffer, or vice versa.

> `loco_yeet_surrounding_function`
Selects the surrounding function then yeets it.

> `loco_yeet_tag`
Queries the user for a string an then searches all buffers for that string
as a comment tag (i.e. "// @tag") and yeets the scope it precedes.

> `loco_yeet_clear`
Clears all current yeets.

> `loco_yeet_reset_all`
Clears all yeets in all slots, also remove the markers from the original buffers.

> `loco_yeet_remove_marker_pair`
Removes a single 'yeet', whatever one the cursor is currently inside.

> `loco_save_snapshot_1`
> `loco_save_snapshot_2`
> `loco_save_snapshot_3`
Saves the current collection of yeets to a slot.
Note that these do not persist if you close the editor.

> `loco_load_yeet_snapshot_1`
> `loco_load_yeet_snapshot_2`
> `loco_load_yeet_snapshot_3`
Loads a yeet snapshot into the yeet buffer.

> `loco_jump_between_yeet`
Will attempt to jump to the corresponding location in the linked buffer.

## CONFIG
There are currently a few global variables in `4coder_loco_yeets.cpp`, their variable names are self-explanatory.