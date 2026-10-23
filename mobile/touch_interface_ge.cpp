// GoldenEye 007 (Project Midas) touch layout: Clibs_OpenTouch/alpha/touch_interface.cpp with a menu mouse
// (tap or drag to move the crosshair, A to select), the menu arrows as a D-pad cross and GoldenEye's own game buttons.

#include "touch_interface.h"
#include "SDL_keycode.h"

extern "C" void GE_MenuMouseTap(float x, float y);
extern "C" void GE_MenuMouseDrag(float dx, float dy);


void TouchInterface::openGLStart()
{
    touchcontrols::gl_startRender();
};

void TouchInterface::openGLEnd()
{
    touchcontrols::gl_endRender();
};

void TouchInterface::mouseMove(int action, float x, float y, float mouse_x, float mouse_y)
{
    if(action == TOUCHMOUSE_MOVE)
    {
        // A drag moves the crosshair from where it is
        GE_MenuMouseDrag(-mouse_x, -mouse_y);
    }
    else if(action == TOUCHMOUSE_TAP)
    {
        // A tap moves the crosshair there; A selects
        GE_MenuMouseTap(x, y);
    }
}

void TouchInterface::createControls(std::string filesPath)
{
    tcMenuMain = new touchcontrols::TouchControls("menu", false, true, 10, false);
    tcYesNo = new touchcontrols::TouchControls("yes_no", false, false);
    tcGameMain = new touchcontrols::TouchControls("game", false, true, 1, true);
    tcGameWeapons = new touchcontrols::TouchControls("weapons", false, true, 1, false);
    tcInventory = new touchcontrols::TouchControls("inventory", false, true, 2, false); //Different edit group
    tcWeaponWheel = new touchcontrols::TouchControls("weapon_wheel", false, true, 1, false);
    tcBlank = new touchcontrols::TouchControls("blank", true, false);
    tcCustomButtons = new touchcontrols::TouchControls("custom_buttons", false, true, 1, true);
    tcKeyboard = new touchcontrols::TouchControls("keyboard", false, false);
    tcGamepadUtility = new touchcontrols::TouchControls("gamepad_utility", false, false);
    tcDPadInventory = new touchcontrols::TouchControls("dpad_inventory", false, false);
    tcMouse = new touchcontrols::TouchControls("mouse", false, false);
    // Hide the cog because when using the gamepad and weapon wheel is enabled, the cog will show otherwise
    tcWeaponWheel->hideEditButton = true;

    //Menu -------------------------------------------
    //------------------------------------------------------
    tcMenuMain->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON));
    // Arrows as a D-pad cross
    tcMenuMain->addControl(new touchcontrols::Button("up_arrow", touchcontrols::RectF(20.5, 8.5, 23, 11), "arrow_up", PORT_ACT_MENU_UP));
    tcMenuMain->addControl(new touchcontrols::Button("left_arrow", touchcontrols::RectF(18, 11, 20.5, 13.5), "arrow_left", PORT_ACT_MENU_LEFT));
    tcMenuMain->addControl(new touchcontrols::Button("right_arrow", touchcontrols::RectF(23, 11, 25.5, 13.5), "arrow_right", PORT_ACT_MENU_RIGHT));
    tcMenuMain->addControl(new touchcontrols::Button("down_arrow", touchcontrols::RectF(20.5, 13.5, 23, 16), "arrow_down", PORT_ACT_MENU_DOWN));
    tcMenuMain->addControl(new touchcontrols::Button("enter", touchcontrols::RectF(1.5, 11.5, 4.5, 14.5), "n64_a_button", PORT_ACT_MENU_SELECT));
    tcMenuMain->addControl(new touchcontrols::Button("keyboard", touchcontrols::RectF(2, 0, 4, 2), "keyboard", KEY_SHOW_KBRD));

    tcMenuMain->addControl(new touchcontrols::Button("gamepad", touchcontrols::RectF(22, 0, 24, 2), "gamepad", KEY_SHOW_GAMEPAD));
    tcMenuMain->addControl(new touchcontrols::Button("gyro", touchcontrols::RectF(24, 0, 26, 2), "gyro", KEY_SHOW_GYRO));
    tcMenuMain->addControl(new touchcontrols::Button("load_save_touch", touchcontrols::RectF(20, 0, 22, 2), "touchscreen_save", KEY_LOAD_SAVE_CONTROLS));

    //tcMenuMain->addControl(new touchcontrols::Button("brightness",touchcontrols::RectF(21,0,23,2),"brightness",KEY_BRIGHTNESS));

    touchcontrols::Mouse *brightnessSlide = new touchcontrols::Mouse("slide_mouse", touchcontrols::RectF(24.5, 3, 26, 11), "brightness_slider");
    brightnessSlide->description = "Brightness slider";
    brightnessSlide->setHidden(true); // Shown from the controls editor's button list
    brightnessSlide->signal_action.connect(sigc::mem_fun(this, &TouchInterface::brightnessSlideMouse));
    tcMenuMain->addControl(brightnessSlide);

    // Menu mouse, last so the buttons above take their touches first
    touchcontrols::Mouse *menuMouse = new touchcontrols::Mouse("menu_mouse", touchcontrols::RectF(0, 0, 26, 16), "");
    menuMouse->setHideGraphics(true);
    menuMouse->setEditable(false);
    menuMouse->signal_action.connect(sigc::mem_fun(this, &TouchInterface::mouseMove));
    tcMenuMain->addControl(menuMouse);

    tcMenuMain->signal_button.connect(sigc::mem_fun(this, &TouchInterface::menuButton));
    tcMenuMain->setAlpha(0.8);
    tcMenuMain->setFixAspect(true);

    //Game -------------------------------------------
    //------------------------------------------------------
    tcGameMain->setAlpha(touchSettings.alpha);
    tcGameMain->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON, false, false, "Watch (pause)"));
    tcGameMain->addControl(new touchcontrols::Button("objectives", touchcontrols::RectF(2, 0, 4, 2), "notebook", PORT_ACT_MAP, false, false, "Objectives"));
    tcGameMain->addControl(new touchcontrols::Button("attack", touchcontrols::RectF(20, 7, 23, 10), "shoot", KEY_SHOOT, false, false, "Fire"));
    tcGameMain->addControl(new touchcontrols::Button("attack2", touchcontrols::RectF(3, 5, 6, 8), "shoot", KEY_SHOOT, false, true, "Fire (duplicate)"));
    tcGameMain->addControl(new touchcontrols::Button("b_button", touchcontrols::RectF(23, 6, 26, 9), "n64_b_button", PORT_ACT_N64_BUTTON_B, false, false, "Use / open (B)"));

    // Far Cry's scope slider: tap to aim, slide up or down to zoom (sniper rifle, camera)
    touchcontrols::QuadSlide *zoomSlide = new touchcontrols::QuadSlide("zoom_slide", touchcontrols::RectF(21, 3, 23, 5), "zoom", "slide_arrow",
                                                                      PORT_ACT_MAP_ZOOM_IN, 0, PORT_ACT_MAP_ZOOM_OUT, 0, false, "Aim (tap), zoom in/out (slide)");
    zoomSlide->setTapValue(PORT_ACT_ZOOM_IN);
    zoomSlide->signal.connect(sigc::mem_fun(this, &TouchInterface::gameButton));
    tcGameMain->addControl(zoomSlide);

    tcGameMain->addControl(new touchcontrols::Button("next_gadget", touchcontrols::RectF(3, 3, 5, 5), "inventory", PORT_ACT_INVEN, false, false, "Next gadget"));
    tcGameMain->addControl(new touchcontrols::Button("crouch", touchcontrols::RectF(24, 14, 26, 16), "crouch", PORT_ACT_DOWN, false, false, "Crouch"));
    tcGameMain->addControl(new touchcontrols::Button("crouch_toggle", touchcontrols::RectF(24, 14, 26, 16), "crouch", PORT_ACT_TOGGLE_CROUCH, false, true, "Crouch (toggle)"));
    tcGameMain->addControl(new touchcontrols::Button("next_weapon", touchcontrols::RectF(0, 3, 3, 5), "next_weap", PORT_ACT_NEXT_WEP, false, false, "Next weapon"));
    tcGameMain->addControl(new touchcontrols::Button("reload", touchcontrols::RectF(0, 5, 3, 7), "reload", PORT_ACT_RELOAD, false, false, "Reload"));
    tcGameMain->addControl(new touchcontrols::Button("prev_weapon", touchcontrols::RectF(0, 7, 3, 9), "prev_weap", PORT_ACT_PREV_WEP, false, false, "Prev weapon"));

    touchJoyRight = new touchcontrols::TouchJoy("touch", touchcontrols::RectF(17, 4, 26, 16), "look_arrow", "fixed_stick_circle");
    tcGameMain->addControl(touchJoyRight);
    touchJoyRight->signal_move.connect(sigc::mem_fun(this, &TouchInterface::rightStick));
    touchJoyRight->signal_double_tap.connect(sigc::mem_fun(this, &TouchInterface::rightDoubleTap));

    touchJoyLeft = new touchcontrols::TouchJoy("stick", touchcontrols::RectF(0, 7, 8, 16), "strafe_arrow", "fixed_stick_circle");
    tcGameMain->addControl(touchJoyLeft);
    touchJoyLeft->signal_move.connect(sigc::mem_fun(this, &TouchInterface::leftStick));
    touchJoyLeft->signal_double_tap.connect(sigc::mem_fun(this, &TouchInterface::leftDoubleTap));

    // SWAPFIX
    touchJoyLeft->registerTouchJoySWAPFIX(touchJoyRight);
    touchJoyRight->registerTouchJoySWAPFIX(touchJoyLeft);

    tcGameMain->signal_button.connect(sigc::mem_fun(this, &TouchInterface::gameButton));
    tcGameMain->signal_settingsButton.connect(sigc::mem_fun(this, &TouchInterface::gameSettingsButton));

    //Weapons -------------------------------------------
    //------------------------------------------------------
    tcGameWeapons->addControl(new touchcontrols::Button("weapon1", touchcontrols::RectF(1, 14, 3, 16), "key_1", 1));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon2", touchcontrols::RectF(3, 14, 5, 16), "key_2", 2));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon3", touchcontrols::RectF(5, 14, 7, 16), "key_3", 3));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon4", touchcontrols::RectF(7, 14, 9, 16), "key_4", 4));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon5", touchcontrols::RectF(9, 14, 11, 16), "key_5", 5));

    tcGameWeapons->addControl(new touchcontrols::Button("weapon6", touchcontrols::RectF(15, 14, 17, 16), "key_6", 6));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon7", touchcontrols::RectF(17, 14, 19, 16), "key_7", 7));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon8", touchcontrols::RectF(19, 14, 21, 16), "key_8", 8));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon9", touchcontrols::RectF(21, 14, 23, 16), "key_9", 9));
    tcGameWeapons->addControl(new touchcontrols::Button("weapon0", touchcontrols::RectF(23, 14, 25, 16), "key_0", 0));

    tcGameWeapons->signal_button.connect(sigc::mem_fun(this, &TouchInterface::selectWeaponButton));
    tcGameWeapons->setAlpha(0.8);

    //Weapon wheel -------------------------------------------
    //------------------------------------------------------

    wheelSelect = new touchcontrols::WheelSelect("weapon_wheel", touchcontrols::RectF(7, 2, 19, 14), "weapon_wheel_%d", wheelNbr);
    wheelSelect->signal_selected.connect(sigc::mem_fun(this, &TouchInterface::weaponWheel));
    wheelSelect->signal_enabled.connect(sigc::mem_fun(this, &TouchInterface::weaponWheelSelected));
    tcWeaponWheel->addControl(wheelSelect);

    if(touchSettings.weaponWheelOpaque)
        tcWeaponWheel->setAlpha(0.8);
    else
        tcWeaponWheel->setAlpha(touchSettings.alpha);

    // Inventory -------------------------------------------
    //------------------------------------------------------

    uiInventoryButtonGrid = new touchcontrols::ButtonGrid("inventory_grid", touchcontrols::RectF(3, 9, 11, 11), "inventory_bg", 4, 1);

    uiInventoryButtonGrid->addCell(0, 0, "inventory_left", PORT_ACT_INVPREV);
    uiInventoryButtonGrid->addCell(1, 0, "inventory_right", PORT_ACT_INVNEXT);
    uiInventoryButtonGrid->addCell(2, 0, "inventory_use", PORT_ACT_INVUSE);
    uiInventoryButtonGrid->addCell(3, 0, "inventory_drop", PORT_ACT_INVDROP);


    uiInventoryButtonGrid->signal_outside.connect(sigc::mem_fun(this, &TouchInterface::inventoryOutside));

    tcInventory->addControl(uiInventoryButtonGrid);
    tcInventory->setPassThroughTouch(touchcontrols::TouchControls::PassThrough::NO_CONTROL);
    tcInventory->signal_button.connect(sigc::mem_fun(this, &TouchInterface::inventoryButton));
    tcInventory->setAlpha(0.9);


    //Blank -------------------------------------------
    //------------------------------------------------------
    tcBlank->addControl(new touchcontrols::Button("enter", touchcontrols::RectF(0, 0, 26, 16), "", 0x123));
    tcBlank->signal_button.connect(sigc::mem_fun(this, &TouchInterface::blankButton));


    //Keyboard -------------------------------------------
    //------------------------------------------------------
    uiKeyboard = new touchcontrols::UI_Keyboard("keyboard", touchcontrols::RectF(0, 8, 26, 16), "font_dual", 0, 0, 0);
    uiKeyboard->signal.connect(sigc::mem_fun(this, &TouchInterface::keyboardKeyPressed));
    tcKeyboard->addControl(uiKeyboard);
    // We want touch to pass through only where there is no keyboard
    tcKeyboard->setPassThroughTouch(touchcontrols::TouchControls::PassThrough::NO_CONTROL);

    //Yes No -------------------------------------------
    //------------------------------------------------------
    tcYesNo->addControl(new touchcontrols::Button("yes", touchcontrols::RectF(8, 12, 11, 15), "key_y", PORT_ACT_MENU_CONFIRM));
    tcYesNo->addControl(new touchcontrols::Button("no", touchcontrols::RectF(15, 12, 18, 15), "key_n", PORT_ACT_MENU_ABORT));
    tcYesNo->signal_button.connect(sigc::mem_fun(this, &TouchInterface::menuButton));
    tcYesNo->setAlpha(0.8);

    //Gamepad utility -------------------------------------------
    //------------------------------------------------------
    touchcontrols::ButtonGrid *gamepadUtils = new touchcontrols::ButtonGrid("gamepad_grid", touchcontrols::RectF(8, 5, 18, 11), "gamepad_utils_bg", 3, 2);

    gamepadUtils->addCell(0, 0, "back_button", KEY_BACK_BUTTON);
    gamepadUtils->addCell(0, 1, "map", PORT_ACT_MAP);
    gamepadUtils->addCell(1, 0, "keyboard", KEY_SHOW_KBRD);
    gamepadUtils->addCell(1, 1, "tild", PORT_ACT_CONSOLE);
    gamepadUtils->addCell(2, 0, "save", PORT_ACT_QUICKSAVE);
    gamepadUtils->addCell(2, 1, "load", PORT_ACT_QUICKLOAD);

    gamepadUtils->signal_outside.connect(sigc::mem_fun(this, &TouchInterface::gameUtilitiesOutside));

    tcGamepadUtility->addControl(gamepadUtils);
    tcGamepadUtility->setAlpha(0.9);
    tcGamepadUtility->signal_button.connect(sigc::mem_fun(this, &TouchInterface::gameUtilitiesButton));

    // DPad inventory -------------------------------------------
    //------------------------------------------------------
    touchcontrols::DPadSelect *dpadInventory = new touchcontrols::DPadSelect("dpad_inventory", touchcontrols::RectF(9, 4, 17, 12), "dpad", PORT_ACT_INVUSE);

    dpadInventory->addCell(touchcontrols::DPAD_LEFT, "inventory_left", PORT_ACT_INVPREV);
    dpadInventory->addCell(touchcontrols::DPAD_RIGHT, "inventory_right", PORT_ACT_INVNEXT);
    dpadInventory->addCell(touchcontrols::DPAD_DOWN, "inventory_drop", PORT_ACT_INVDROP);
    //dpadInventory->addCell(touchcontrols::DPAD_LEFT,"inventory_left",PORT_ACT_INVPREV);
    //
    dpadInventory->signal_button.connect(sigc::mem_fun(this, &TouchInterface::dPadInventoryButton));
    dpadInventory->signal_outside.connect(sigc::mem_fun(this, &TouchInterface::dPadInventoryOutside));

    tcDPadInventory->addControl(dpadInventory);
    tcDPadInventory->setAlpha(0.9);

    // Mouse for GZDoom
    touchcontrols::Mouse *mouse = new touchcontrols::Mouse("mouse", touchcontrols::RectF(0, 0, 26, 16), "");
    mouse->setHideGraphics(true);
    mouse->setEditable(false);
    tcMouse->addControl(mouse);
    mouse->signal_action.connect(sigc::mem_fun(this, &TouchInterface::mouseMove));
    tcMouse->addControl(new touchcontrols::Button("back", touchcontrols::RectF(0, 0, 2, 2), "back_button", KEY_BACK_BUTTON, false, false, "Back"));
    tcMouse->addControl(new touchcontrols::Button("left_button", touchcontrols::RectF(0, 6, 3, 10), "left_mouse", KEY_LEFT_MOUSE, false, false, "Back"));
    tcMouse->signal_button.connect(sigc::mem_fun(this, &TouchInterface::mouseButton));

    std::string
    newSettings = (std::string) filesPath + "/touch_settings_"
    ENGINE_NAME
    ".xml";

    // Enable the Mouse Look setting for Doom engine games
    touchcontrols::tTouchSettingsModifier modifier;
    modifier.mouseLookVisible = true;

    UI_tc = touchcontrols::createDefaultSettingsUI(&controlsContainer, newSettings, &modifier);
    UI_tc->setAlpha(1);

    //---------------------------------------------------------------
    //---------------------------------------------------------------
    controlsContainer.addControlGroup(tcKeyboard);
    controlsContainer.addControlGroup(tcInventory); // before gamemain so touches don't go through
    controlsContainer.addControlGroup(tcGamepadUtility); // before gamemain so touches don't go through
    controlsContainer.addControlGroup(tcDPadInventory);
    controlsContainer.addControlGroup(tcCustomButtons);
    controlsContainer.addControlGroup(tcGameMain);
    controlsContainer.addControlGroup(tcYesNo);
    controlsContainer.addControlGroup(tcGameWeapons);
    controlsContainer.addControlGroup(tcMenuMain);
    controlsContainer.addControlGroup(tcWeaponWheel);
    controlsContainer.addControlGroup(tcBlank);
    controlsContainer.addControlGroup(tcMouse);

    // Its own file: a menu.xml saved by another engine would put the arrows back in a row
    tcMenuMain->setXMLFile((std::string) filesPath + "/menu_"
    ENGINE_NAME
    ".xml");

    tcGameMain->setXMLFile((std::string) filesPath + "/game_"
    ENGINE_NAME
    ".xml");
    tcInventory->setXMLFile((std::string) filesPath + "/inventory_"
    ENGINE_NAME
    ".xml");
    tcWeaponWheel->setXMLFile((std::string) filesPath + "/weaponwheel_"
    ENGINE_NAME
    ".xml");
    tcGameWeapons->setXMLFile((std::string) filesPath + "/weapons_"
    ENGINE_NAME
    ".xml");
    tcCustomButtons->setXMLFile((std::string) filesPath + "/custom_buttons_0_"
    ENGINE_NAME
    ".xml");
}

void TouchInterface::blankButton(int state, int code)
{
    PortableKeyEvent(state, SDL_SCANCODE_SPACE, 0);
}

void TouchInterface::automapButton(int state, int code)
{

}

void TouchInterface::newFrame()
{
    touchscreemode_t screenMode = PortableGetScreenMode();

    // Hack to show custom buttons while in the menu to bind keys
    if(screenMode == TS_MENU && showCustomMenu == true)
    {
        screenMode = TS_CUSTOM;
    }

    if((screenMode == TS_MAP) && (mapState == 1))
    {
        screenMode = TS_GAME;
    }

    updateTouchScreenModeOut(screenMode);
    updateTouchScreenModeIn(screenMode);

    currentScreenMode = screenMode;
}

void TouchInterface::newGLContext()
{

}