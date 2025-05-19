inline_function b32 
UI_Widget_IsEmpty(UI_Widget* widget) {
    return IsNull(widget) || widget == g_ui_widget;
}

inline_function void UI_Widget_TreeStateReset(UI_Widget* widget) {
    widget->first = g_ui_widget;
    widget->last = g_ui_widget;
    widget->next = g_ui_widget;
    widget->prev = g_ui_widget;
    widget->parent = g_ui_widget;

}

root_function UI_Widget*
UI_Widget_FromKey(UI_State* ui_state, UI_Key key)
{
    UI_Widget* result = {0};
    u64 slotKey = key.key % ui_state->widgetCacheSize;
    UI_WidgetSlot* slot = &ui_state->widgetSlot[slotKey];

    for (UI_Widget* widget = slot->first; !UI_Widget_IsEmpty(widget); widget = widget->hashNext)
    {
        if (UI_Key_IsEqual(widget->key, key))
        {
            result = widget;
            break;
        }
    }

    if (UI_Widget_IsEmpty(result))
    {
        result = UI_WidgetSlot_Push(ui_state, key);
    }
    return result;
}

root_function UI_Widget*
UI_Widget_Allocate(UI_State* ui_state)
{
    UI_Widget* widget = ui_state->freeList;
    if (ui_state->freeListSize > 0)
    {
        StackPop(ui_state->freeList);
        MemoryZeroStruct(widget);
        ui_state->freeListSize -= 1;
    }
    else
    {
        widget = PushStruct(ui_state->arena_permanent, UI_Widget);
    }
    return widget;
}

// UI_WidgetSlot
root_function UI_Widget*
UI_WidgetSlot_Push(UI_State* ui_state, UI_Key key)
{
    UI_WidgetSlot* slot = &ui_state->widgetSlot[key.key % ui_state->widgetCacheSize];
    UI_Widget* widget = UI_Widget_Allocate(ui_state);

    DLLPushBack_NPZ(slot->first, slot->last, widget, hashNext, hashPrev, UI_Widget_IsEmpty, SetNull);
    widget->key = key;
    return widget;
}

// UI_Key
root_function UI_Key
UI_Key_Calculate(String8 str)
{
    u128 hash = HashFromStr8(str);
    UI_Key key = {hash.data[1]};
    return key;
}

root_function bool
UI_Key_IsEqual(UI_Key key0, UI_Key key1)
{
    return key0.key == key1.key;
}

root_function bool
UI_Key_IsNull(UI_Key key)
{
    return key.key == 0;
}

// Widget implementations

root_function UI_Widget*
UI_Widget_DepthFirstPreOrder(UI_Widget* widget)
{
    UI_Widget* next = {0};
    if (!UI_Widget_IsEmpty(widget->first))
    {
        next = widget->first;
    }
    else
        for (UI_Widget* parent = widget; !UI_Widget_IsEmpty(parent); parent = parent->parent)
        {
            if (!UI_Widget_IsEmpty(parent->next))
            {
                next = parent->next;
                break;
            }
        }
    return next;
}

inline_function UI_Widget*
UI_Widget_TreeLeftMostFind(UI_Widget* root)
{
    UI_Widget* child = root;
    for (; !UI_Widget_IsEmpty(child) && !UI_Widget_IsEmpty(child->first); child = child->first)
    {
    };
    return child;
}

root_function UI_Widget*
UI_Widget_DepthFirstPostOrder(UI_Widget* widget)
{
    UI_Widget* next = {0};
    if (UI_Widget_IsEmpty(widget->next))
    {
        next = widget->parent;
    }
    else
        for (next = widget->next; !UI_Widget_IsEmpty(next->first); next = next->first)
        {
        }
    return next;
}

typedef u32 IterState;
enum
{
    UI_IterState_PRE,
    UI_IterState_POST,
    UI_IterState_Count
};

struct UI_WidgetDF
{
    UI_Widget* next;
    IterState iterState;
};



root_function void
UI_Widget_SizeAndRelativePositionCalculate(GlyphAtlas* glyphAtlas, UI_State* ui_state)
{
    UI_Widget* leftMostWidget = UI_Widget_TreeLeftMostFind(ui_state->root);
    // size and relative position calculation
    for (UI_Widget* widget = leftMostWidget; !UI_Widget_IsEmpty(widget);
         widget = UI_Widget_DepthFirstPostOrder(widget))
    {
        widget->computedSize = {0.0f, 0.0f};
        widget->computedRelativePosition = {0.0f, 0.0f};

        if (widget->semanticSize[Axis2_X].kind == UI_SizeKind_TextContent ||
            widget->semanticSize[Axis2_Y].kind == UI_SizeKind_TextContent)
        {
            Vec2<f32> text_size = widget->text_ext->size_calc_func(widget);
            widget->computedSize = text_size;
        }

        for (u32 axis = 0; axis < Axis2_COUNT; axis++)
        {
            switch(widget->semanticSize[axis].kind) {

                case UI_SizeKind_ChildrenSum:
                {
                    for (UI_Widget* child = widget->first; !UI_Widget_IsEmpty(child); child = child->next)
                    {
                        child->computedRelativePosition[axis] = widget->computedSize[axis];
                        widget->computedSize[axis] += child->computedSize[axis];
                    }

                } break;
                case UI_SizeKind_Pixels: 
                {
                    widget->computedSize[axis] = widget->semanticSize[axis].value;
                } break;
                case UI_SizeKind_Null:
                {
                    for (UI_Widget* child = widget->first; !UI_Widget_IsEmpty(child); child = child->next)
                    {
                        widget->computedSize[axis] =
                            Max(widget->computedSize[axis], child->computedSize[axis]);
                    }
                } break;
            }

        }
    }
}

root_function f32 ResizeChildren(UI_Widget* widget, Axis2 axis, f32 AxChildSizeCum, UI_Size parentSemanticSizeInfo, b32 useStrictness) {
    b32 resizingDone = 0;
    f32 strictness = 0;
    f32 sizeCum = AxChildSizeCum;
    for (UI_Widget* child = widget->last; !UI_Widget_IsEmpty(child); child = child->prev) {
        UI_Size childSemanticSizeInfo = child->semanticSize[axis];
        if (useStrictness) {
            strictness = childSemanticSizeInfo.strictness;
        }
        sizeCum -= Max(child->computedSize[axis] - strictness, 0);
        child->computedSize[axis] = strictness;
        f32 sizeDiff = parentSemanticSizeInfo.value - sizeCum;
        if (sizeDiff >= 0) {
            sizeCum += sizeDiff;
            child->computedSize[axis] += sizeDiff;
            resizingDone=1;
            break;
        }
    }
    return sizeCum;
}

root_function void ReassignRelativePositionsOfChildren(UI_Widget* widget, Axis2 axis) {
    f32 sizeCum = 0;
    for (UI_Widget* child = widget->first; !UI_Widget_IsEmpty(child); child = child->next) {
        child->computedRelativePosition[axis] = sizeCum;
        sizeCum += child->computedSize[axis];
    }
}

root_function void
UI_Widget_AbsolutePositionCalculate(UI_State* ui_state, F32Vec4 posAbs)
{
    for (UI_Widget* widget = ui_state->root; !UI_Widget_IsEmpty(widget);
         widget = UI_Widget_DepthFirstPreOrder(widget))
    {
        widget->rect = {0};

        F32Vec4 rectParent = posAbs;
        if (widget != ui_state->root)
        {
            rectParent = widget->parent->rect;
        }

        for (u32 axis = 0; axis < Axis2_COUNT; axis++) {
            UI_Size semanticSizeInfo = widget->semanticSize[axis];
            switch (semanticSizeInfo.kind) {
                case UI_SizeKind_Pixels:{
                    f32 childAxSizeTotal = 0;
                    for (UI_Widget* child = widget->first; !UI_Widget_IsEmpty(child); child = child->next) {
                        childAxSizeTotal += child->computedSize[axis];
                    }
                    if (childAxSizeTotal > semanticSizeInfo.value) {
                        b32 useStrictness = 1;
                        
                        f32 sizeAfterStrictnessResize = ResizeChildren(widget,(Axis2)axis, childAxSizeTotal, semanticSizeInfo, useStrictness); 
                        if (sizeAfterStrictnessResize > semanticSizeInfo.value) {
                            useStrictness = 0;
                            ResizeChildren(widget,(Axis2)axis, sizeAfterStrictnessResize, semanticSizeInfo, useStrictness); 
                        }
                    }
                    ReassignRelativePositionsOfChildren(widget, (Axis2)axis);
                } break;
                case UI_SizeKind_PercentOfParent: {
                    ASSERT(0.0f <= semanticSizeInfo.value && semanticSizeInfo.value <= 1.0f, "Semantic value must be a value between 0 and 1");
                    f32 parentSize = rectParent.point.p1[axis] - rectParent.point.p0[axis];
                    widget->computedSize[axis] = parentSize * semanticSizeInfo.value;
                    f32 computedRelativePosition = {0};
                    if(!UI_Widget_IsEmpty(widget->prev)) {
                        computedRelativePosition = widget->prev->computedRelativePosition[axis] + widget->prev->computedSize[axis]; 
                    }
                    widget->computedRelativePosition[axis] = computedRelativePosition;
                } break;
                case UI_SizeKind_Null:
                {
                    u32 otherAx = (axis + 1) % Axis2_COUNT;
                    if (widget->semanticSize[otherAx].kind == UI_SizeKind_PercentOfParent) {
                        widget->computedSize[axis] = rectParent.point.p1[axis] - rectParent.point.p0[axis];
                    }
                } break;
            }
        }

        widget->rect.point.p0 =
            widget->computedRelativePosition + rectParent.point.p0;
        widget->rect.point.p1 = widget->rect.point.p0 + widget->computedSize;
    }
}

root_function void
UI_Widget_DrawPrepare(Arena* arena, UI_State* ui_state, BoxContext* box_context)
{
    for (UI_Widget* widget = ui_state->root; !UI_Widget_IsEmpty(widget);
         widget = UI_Widget_DepthFirstPreOrder(widget))
    {
        if (widget->flags & UI_WidgetFlag_DrawBackground)
        {
            widget->rect_ext->draw_func(widget);
        }

        if (widget->flags & UI_WidgetFlag_DrawText)
        {
            widget->text_ext->draw_func(widget);
        }
    }
}

// layout
inline_function void
UI_PushLayout()
{
    UI_State* ui_state = GlobalContextGet()->ui_state;
    ASSERT(!UI_Widget_IsEmpty(ui_state->current), "Cannot push a layout without a widget to promote to parent");
    C_Parent_Push(ui_state->current);
}

inline_function void
UI_PopLayout()
{
    UI_State* ui_state = GlobalContextGet()->ui_state;
    ui_state->root = ui_state->current = C_Parent_Get();
    C_Parent_Pop();
}

inline_function void
UI_State_FrameReset(UI_State* ui_state)
{
    ui_state->current = g_ui_widget;
    ui_state->root = g_ui_widget;
    ArenaReset(ui_state->arena_frame);
}

// Text Extensions ---------------------------------------------------------------

root_function Vec2<f32>
UI_TextExtSizeCalc(UI_Widget* widget) {
    GlyphAtlas* glyphAtlas = GlobalContextGet()->glyphAtlas;
    UI_TextExtData* data = (UI_TextExtData*)widget->text_ext->data;
    Font* font = FontFindOrCreate(glyphAtlas, data->font_size);
    Vec2<f32> text_size = TextDimensionsCalculate(font, data->text);
    data->text_size = text_size;

    f32 padding_width = data->padding.point.p0.x + data->padding.point.p1.x;
    f32 padding_height = data->padding.point.p0.y + data->padding.point.p1.y;
    Vec2<f32> padding_size = {padding_width, padding_height};

    f32 margin_width = data->margin.point.p0.x + data->margin.point.p1.x;
    f32 margin_height = data->margin.point.p0.y + data->margin.point.p1.y;
    Vec2<f32> margin_size = {margin_width, margin_height};

    return data->text_size + 2.f*data->border_thickness + padding_size + margin_size;
}

root_function void
UI_TextExtDraw(UI_Widget* widget) {
    GlyphAtlas* glyphAtlas = GlobalContextGet()->glyphAtlas;
    UI_TextExtData* data = (UI_TextExtData*)widget->text_ext->data;
    Vec2 diff_dim = widget->rect.point.p1 - widget->rect.point.p0 - data->text_size;
    Vec2 glyph_pos = widget->rect.point.p0 + diff_dim / 2.0f;
    Vec2 p0xB = widget->rect.point.p0 + data->border_thickness + data->padding.point.p0 + data->margin.point.p0;
    Vec2 p1xB = widget->rect.point.p1 - data->border_thickness - data->padding.point.p1 - data->margin.point.p1;

    Font *font = FontFindOrCreate(glyphAtlas, data->font_size);

    TextDraw(font, data->text, p0xB, p1xB);
} 

root_function void 
UI_Widget_TextExtAdd(UI_Widget* widget, UI_TextExtSizeCalcFuncType* size_calc_func, UI_TextExtDrawFuncType* draw_func) {

    UI_State* ui_state = GlobalContextGet()->ui_state;
    Arena* arena = ui_state->arena_frame;

    u32 font_size = C_FontSize_Get();
    String8 text = C_Text_Get();
    f32 border_thickness = C_BorderThickness_Get();
    F32Vec4 padding = C_Padding_Get();
    F32Vec4 margin = C_Margin_Get();
    UI_TextExtData* data = PushStruct(arena, UI_TextExtData);
    *data = {.font_size=font_size, .text=text, .border_thickness=border_thickness, .padding=padding, .margin=margin};

    UI_TextExt* text_ext = PushStruct(arena, UI_TextExt);
    text_ext->data = data;
    text_ext->size_calc_func = size_calc_func;
    text_ext->draw_func = draw_func;
    widget->text_ext = text_ext;
}

// Rect extension ----------------------------------------------------------------
root_function void
UI_Widget_RectExtDraw(UI_Widget* widget) {
    UI_RectExtData* data = (UI_RectExtData*)widget->rect_ext->data;
    Context* ctx = GlobalContextGet();
    Arena* frame_arena = ctx->ui_state->arena_frame;
    BoxContext* box_context = ctx->box_context;
    Box* box = PushStructZero(frame_arena, Box);
    QueuePush(box_context->boxQueue.first, box_context->boxQueue.last, box);

    // reacting to last frame input
    box->pos0 = widget->rect.point.p0 + data->margin.point.p0;
    box->pos1 = widget->rect.point.p1 - data->margin.point.p1;
    box->color = data->background_color;
    box->softness = data->softness;
    box->borderThickness = data->border_thickness;
    box->cornerRadius = data->corner_radius;
    box->attributes = 0;

    if (widget->flags & UI_WidgetFlag_Clickable)
    {
        if (widget->hot_t)
        {
            box->attributes |= BoxAttributes::HOT;
            if (widget->active_t)
            {
                box->attributes |= BoxAttributes::ACTIVE;
            }
        }
    }
}

root_function void 
UI_Widget_RectExtAdd(UI_Widget* widget, UI_RectExtDrawFuncType* draw_func) {

    UI_State* ui_state = GlobalContextGet()->ui_state;
    Arena* arena = ui_state->arena_frame;

    F32Vec4 color = C_BackgroundColor_Get();
    f32 softness = C_Softness_Get();
    f32 border_thickness = C_BorderThickness_Get();
    f32 corner_radius = C_CornerRadius_Get();
    F32Vec4 margin = C_Margin_Get();
    UI_RectExtData* data = PushStruct(arena, UI_RectExtData);
    *data = {
            .background_color=color, 
            .softness=softness,
            .border_thickness=border_thickness, 
            .corner_radius=corner_radius, 
            .margin=margin
        };

    UI_RectExt* rect_ext = PushStruct(arena, UI_RectExt);
    rect_ext->data = data;
    rect_ext->draw_func = draw_func;
    widget->rect_ext = rect_ext;
}

// Root Functions ----------------------------------------------------------------
root_function void
UI_Widget_Add(String8 widgetName, UI_WidgetFlags flags,
                UI_Size semanticSizeX, UI_Size semanticSizeY)
{
    Context* context = GlobalContextGet();
    UI_State* ui_state = context->ui_state;
    UI_IO* io = context->io;
    GlyphAtlas* glyphAtlas = context->glyphAtlas;

    UI_Key key = UI_Key_Calculate(widgetName);
    UI_Widget* widget = UI_Widget_FromKey(ui_state, key);
    
    UI_Widget_TreeStateReset(widget);

    widget->name = widgetName;
    widget->flags = flags;
    widget->active_t = false;
    widget->hot_t = false;
    widget->semanticSize[Axis2_X] = semanticSizeX;
    widget->semanticSize[Axis2_Y] = semanticSizeY;

    if (flags & UI_WidgetFlag_DrawBackground) {
        UI_Widget_RectExtAdd(widget, UI_Widget_RectExtDraw);
    }

    if (flags & UI_WidgetFlag_DrawText) {
        UI_Widget_TextExtAdd(widget, UI_TextExtSizeCalc, UI_TextExtDraw);
    }

    if (io->mousePosition >= widget->rect.point.p0 && io->mousePosition <= widget->rect.point.p1)
    {
        widget->hot_t = true;
        if (io->leftClicked)
        {
            widget->active_t = true;
        }
    }

    UI_Widget* parent = C_Parent_Get();
    if (!UI_Widget_IsEmpty(parent))
    {
        // add to widget tree
        DLLPushBack_NPZ(parent->first, parent->last, widget, next, prev, UI_Widget_IsEmpty, SetNull);
    }

    ASSERT((UI_Widget_IsEmpty(parent) && UI_Widget_IsEmpty(ui_state->current)) || !UI_Widget_IsEmpty(parent),
           "Tree can only have one widget root");

    widget->parent = parent;
    ui_state->current = widget;
}
