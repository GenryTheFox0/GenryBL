// GenryBL V2 - mechanics ES never had, made of ES's own parts so they look born in the game.
//  «кодовыйзамок»: ES's o_rly plate (the paper strip over the darkened game, like its yes/no prompt): the question and the
//                  cells of the code in brown on the paper, the digits and «Ввести» below in settings_link, like the
//                  plate of the hero's name; typed by the mouse or the keyboard, a wrong code shakes red.
//  «фонарик»:      7ДЛ's flashlight - the dark lies on the master layer (under the dialogue box), a soft circle of light
//                  follows the mouse, the wheel makes it wider or narrower.
// Appended to a mod only when it uses them.
#pragma once

static const char* const kV2CodeLock = R"rpy(
# GenryBL V2: кодовый замок на табличке БЛ (o_rly) - мышкой или цифрами клавиатуры, неверный код трясётся красным
transform genry_code_shake:
    xoffset 0
    ease 0.05 xoffset -16
    ease 0.05 xoffset 16
    ease 0.05 xoffset -9
    ease 0.05 xoffset 9
    ease 0.05 xoffset 0

transform genry_code_still:
    xoffset 0

screen genry_codelock(code, hint=u"", tries=0):
    modal True
    zorder 200
    default genry_typed = u""
    default genry_left = tries
    default genry_wrong = False
    $ genry_try = If(genry_typed == code, Return(True), [SetScreenVariable("genry_wrong", True), SetScreenVariable("genry_typed", u""), SetScreenVariable("genry_left", genry_left - 1), If(tries > 0 and genry_left <= 1, Return(False))])
    $ genry_put = lambda d: [SetScreenVariable("genry_typed", (genry_typed + d)[:len(code)]), SetScreenVariable("genry_wrong", False)]
    add get_image("gui/o_rly/base.png")
    text (hint or u"Какой код?") text_align 0.5 xalign 0.5 yalign 0.415 color "#64483c" font header_font size 32
    text u"  ".join([(genry_typed[i] if i < len(genry_typed) else u"_") for i in range(len(code))]) at (genry_code_shake if genry_wrong else genry_code_still):
        xalign 0.5
        yalign 0.5
        color ("#b3001b" if genry_wrong else "#3f2a1d")
        font header_font
        size 60
    hbox:
        xalign 0.5
        yalign 0.63
        spacing 24
        for genry_d in [u"1", u"2", u"3", u"4", u"5", u"6", u"7", u"8", u"9", u"0"]:
            textbutton genry_d style "log_button" text_style "settings_link" text_size 54 action genry_put(genry_d) activate_sound "sound/sfx/click_1.ogg"
        textbutton u"←" style "log_button" text_style "settings_link" text_size 54 action [SetScreenVariable("genry_typed", genry_typed[:-1]), SetScreenVariable("genry_wrong", False)]
    hbox:
        xalign 0.5
        yalign 0.735
        spacing 90
        textbutton u"Ввести" style "log_button" text_style "settings_link" text_size 60 action genry_try activate_sound "sound/sfx/click_1.ogg"
        textbutton u"Уйти" style "log_button" text_style "settings_link" text_size 44 action Return(False)
    if tries > 0:
        text (u"попыток осталось: %d" % genry_left) xalign 0.5 yalign 0.81 color ("#b3001b" if genry_left <= 1 else "#909ca3") font header_font size 26
    for genry_k in range(10):
        key ("K_%d" % genry_k) action genry_put(str(genry_k))
        key ("K_KP%d" % genry_k) action genry_put(str(genry_k))
    key "K_BACKSPACE" action [SetScreenVariable("genry_typed", genry_typed[:-1]), SetScreenVariable("genry_wrong", False)]
    key "K_RETURN" action genry_try
    key "K_KP_ENTER" action genry_try
)rpy";

static const char* const kV2Flashlight = R"rpy(
# GenryBL V2: фонарик (как в 7ДЛ) - темнота под окном реплик, пятно света за мышью, колёсико - шире / уже
init python:
    def genry_light_f(trans, st, at):
        x, y = renpy.get_mouse_pos()
        trans.xpos, trans.ypos = int(x), int(y)
        return 1.0 / 30

transform genry_light_at(z=1.5):
    anchor (0.5, 0.5)
    zoom z
    function genry_light_f

screen genry_flashlight(mask, color="#050810", z=1.5):
    layer "master"
    zorder 200
    default genry_z = z
    key "rollback" action SetScreenVariable("genry_z", min(2.4, genry_z + 0.1))
    key "rollforward" action SetScreenVariable("genry_z", max(1.0, genry_z - 0.1))
    add Transform(mask, matrixcolor=TintMatrix(color)) at genry_light_at(genry_z)
)rpy";
