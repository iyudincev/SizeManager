Macro {
  area="Dialog";
  key="Enter Home End Up Down PgUp PgDn Left Right CtrlPgUp CtrlPgDn";
  flags="NoPluginPanels|NoPluginPPanels|NoSendKeysToPlugins";
  priority=0;
  description="Size Manager - panel navigation";
  action = function()
    if Dlg.Id == "E41F6EFF-49DA-40D8-BB50-37D355D812CC" or
       Dlg.Id == "5431982E-24CA-4BAC-8831-177300C2405C"
    then
      Keys("Esc")
      Keys(akey(1))
      Plugin.Call("F36E3C60-C77F-43F2-83C1-8A879DDBBCD7", 0)
    else
      Keys(akey(1))
    end
  end;
}
