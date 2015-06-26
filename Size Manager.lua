-- ������ �������� �� LAltShiftS. ������ ���������� � �������� ��������
-- ������� � ������ ���������, ��������� �������� ������ � ������ �������.
--
-- (c) VictorVG @ VikSoft.Ru, 2014 - 2015 , (c) Igor Yudincev, 2015
-- ������� ������:
--
-- v1.0 - ������ ������, ������ ����� ������, ��������� � �� ��� �� ����
-- 02.08.2014, 14:11:44 +0300
-- v1.1 - ��������� ��������� �� Size Manager.lua by Igor Yudincev.
-- 18.04.2015, 15:00:27 +0300
-- v1.2 - �����������, �������� ��������� �������� � ArcLite (���-������ skipik)
-- Wed Jun 24 04:05:28 +0300 2015
-- v1.3 - �����������, �������� ��������� �������� � F5/F6/F8 (���-������ skipik)
-- Thu Jun 25 13:55:50 +0300 2015
-- v1.4 - �����������
-- Thu Jun 25 23:30:20 +0300 2015
-- v1.5 - �����������: ����� ������������� ���, ������ ��������� ������ �������
-- Fri Jun 26 00:43:15 +0300 2015
--

local SMId="F36E3C60-C77F-43F2-83C1-8A879DDBBCD7";
local SMMId="F3D9C64A-BC7A-49A5-8FD9-38CAE5A37282";
local DlgId1="E41F6EFF-49DA-40D8-BB50-37D355D812CC";
local DlgId2="5431982E-24CA-4BAC-8831-177300C2405C";

Macro {
  area="Shell Tree";
  key="LAltShiftS";
  flags="NoPluginPanels NoPluginPPanels NoSendKeysToPlugins";
  description="Size Manager: run plugin";
  action=function()
    Plugin.Menu(SMId,SMMId)
  end;
}

Macro {
  area="Dialog";
  key="Enter Home End Up Down PgUp PgDn Left Right CtrlPgUp CtrlPgDn";
  flags="NoPluginPanels NoPluginPPanels NoSendKeysToPlugins";
  priority=0;
  description="Size Manager: panel navigation";
  condition=function() return Dlg.Id==(DlgId1 or DlgId2) end;
  action = function()
    Keys("Esc")
    Keys(akey(1))
    Plugin.Call(SMId,0)
  end;
}
