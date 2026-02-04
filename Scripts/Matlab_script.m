function conveyor_gui_matlab()
% MATLAB GUI monitor + control (Jetson TCP client)
% RX telemetry unchanged: logs "RX: <line>"
% TX commands: START/STOP/HOME/PWM + TARGET
% UI: PWM slider 0..255 (default 255) + PWM +/- step 20 + Target +/- step 1


clear; clc;

%% ================= CONFIG =================
JETSON_IP   = "192.168.1.73";
JETSON_PORT = 65432;

%% ================= GUI SETUP =================
fig = uifigure('Name','Conveyor TCP Dashboard','Position',[120 120 1050 620]);
fig.Color = [0.94 0.94 0.94];

gl = uigridlayout(fig,[2 2]);
gl.RowHeight = {215,'1x'};
gl.ColumnWidth = {'1x','1x'};
gl.Padding = [8 8 8 8];
gl.RowSpacing = 8;
gl.ColumnSpacing = 8;
gl.BackgroundColor = [0.94 0.94 0.94];

%% ================= CONTROLS =================
pCtrl = uipanel(gl,'Title','Controls','FontWeight','bold');
pCtrl.Layout.Row = 1;
pCtrl.Layout.Column = 1;
pCtrl.BackgroundColor = [0.97 0.97 0.97];
pCtrl.ForegroundColor = [0 0 0];

gCtrl = uigridlayout(pCtrl,[4 6]);
gCtrl.RowHeight = {45 45 40 45};
gCtrl.ColumnWidth = {'1.2x','1.2x','1x','1x','1.5x','1.5x'};
gCtrl.Padding = [8 8 8 8];
gCtrl.RowSpacing = 6;
gCtrl.ColumnSpacing = 8;
gCtrl.BackgroundColor = [0.97 0.97 0.97];

% Row 1: START / STOP / HOME
btnStart = uibutton(gCtrl,'Text','START','FontSize',13,'FontWeight','bold');
btnStart.BackgroundColor = [0.12 0.60 0.25];
btnStart.FontColor = [1 1 1];
btnStart.Layout.Row = 1; btnStart.Layout.Column = [1 2];

btnStop = uibutton(gCtrl,'Text','STOP','FontSize',13,'FontWeight','bold');
btnStop.BackgroundColor = [0.75 0.15 0.15];
btnStop.FontColor = [1 1 1];
btnStop.Layout.Row = 1; btnStop.Layout.Column = [3 4];

btnHome = uibutton(gCtrl,'Text','HOME (Homing)','FontSize',13,'FontWeight','bold');
btnHome.BackgroundColor = [0.15 0.35 0.75];
btnHome.FontColor = [1 1 1];
btnHome.Layout.Row = 1; btnHome.Layout.Column = [5 6];

% Row 2: PWM label, -, value, +, Send PWM
lblPwm = uilabel(gCtrl,'Text','PWM (0..255)','FontWeight','bold');
lblPwm.Layout.Row = 2; lblPwm.Layout.Column = 1;

btnPwmMinus = uibutton(gCtrl,'Text','-','FontSize',16,'FontWeight','bold');
btnPwmMinus.Layout.Row = 2; btnPwmMinus.Layout.Column = 2;

editPwm = uieditfield(gCtrl,'numeric','Limits',[0 255],'Value',255,'RoundFractionalValues','on');
editPwm.Layout.Row = 2; editPwm.Layout.Column = 3;

btnPwmPlus = uibutton(gCtrl,'Text','+','FontSize',16,'FontWeight','bold');
btnPwmPlus.Layout.Row = 2; btnPwmPlus.Layout.Column = 4;

btnSendPwm = uibutton(gCtrl,'Text','Send PWM','FontWeight','bold');
btnSendPwm.BackgroundColor = [0.25 0.25 0.25];
btnSendPwm.FontColor = [1 1 1];
btnSendPwm.Layout.Row = 2; btnSendPwm.Layout.Column = [5 6];

% Row 3: PWM slider
lblSlider = uilabel(gCtrl,'Text','PWM Slider','FontWeight','bold');
lblSlider.Layout.Row = 3; lblSlider.Layout.Column = 1;

pwmSlider = uislider(gCtrl,'Limits',[0 255],'Value',255);
pwmSlider.MajorTicks = 0:51:255;
pwmSlider.MinorTicks = 0:5:255;
pwmSlider.Layout.Row = 3; pwmSlider.Layout.Column = [2 6];

% Row 4: Target label, -, value, +, Add Target
lblTarget = uilabel(gCtrl,'Text','Target Items','FontWeight','bold');
lblTarget.Layout.Row = 4; lblTarget.Layout.Column = 1;

btnTarMinus = uibutton(gCtrl,'Text','-','FontSize',16,'FontWeight','bold');
btnTarMinus.Layout.Row = 4; btnTarMinus.Layout.Column = 2;

editTarget = uieditfield(gCtrl,'numeric','Limits',[0 Inf],'Value',0,'RoundFractionalValues','on');
editTarget.Layout.Row = 4; editTarget.Layout.Column = 3;

btnTarPlus = uibutton(gCtrl,'Text','+','FontSize',16,'FontWeight','bold');
btnTarPlus.Layout.Row = 4; btnTarPlus.Layout.Column = 4;

btnAddTarget = uibutton(gCtrl,'Text','Add Target','FontWeight','bold');
btnAddTarget.BackgroundColor = [0.25 0.25 0.25];
btnAddTarget.FontColor = [1 1 1];
btnAddTarget.Layout.Row = 4; btnAddTarget.Layout.Column = [5 6];

%% ================= LIVE STATUS =================
pLive = uipanel(gl,'Title','Live Status','FontWeight','bold');
pLive.Layout.Row = 1; pLive.Layout.Column = 2;
pLive.BackgroundColor = [0.97 0.97 0.97];
pLive.ForegroundColor = [0 0 0];

gLive = uigridlayout(pLive,[4 4]);
gLive.RowHeight = {32 32 32 32};
gLive.ColumnWidth = {'1x','1x','1x','1x'};
gLive.Padding = [8 8 8 8];

lampConn = uilamp(gLive,'Color',[0.6 0.6 0.6]);
lampConn.Layout.Row = 1; lampConn.Layout.Column = 1;

lblConn = uilabel(gLive,'Text','Disconnected','FontWeight','bold');
lblConn.Layout.Row = 1; lblConn.Layout.Column = 2;

lampRun = uilamp(gLive,'Color',[0.6 0.6 0.6]);
lampRun.Layout.Row = 1; lampRun.Layout.Column = 3;

lblRun = uilabel(gLive,'Text','Unknown','FontWeight','bold');
lblRun.Layout.Row = 1; lblRun.Layout.Column = 4;

lblState = uilabel(gLive,'Text','State: -','FontWeight','bold');
lblState.Layout.Row = 2; lblState.Layout.Column = [1 2];

lblDist = uilabel(gLive,'Text','Distance (cm): -','FontWeight','bold');
lblDist.Layout.Row = 2; lblDist.Layout.Column = [3 4];

lblRpm = uilabel(gLive,'Text','Speed (rpm): -','FontWeight','bold');
lblRpm.Layout.Row = 3; lblRpm.Layout.Column = [1 2];

lblItems = uilabel(gLive,'Text','Items: -','FontWeight','bold');
lblItems.Layout.Row = 3; lblItems.Layout.Column = [3 4];

lblInt = uilabel(gLive,'Text','Interval (ms): -','FontWeight','bold');
lblInt.Layout.Row = 4; lblInt.Layout.Column = [1 2];

lblRtt = uilabel(gLive,'Text','RTT (ms): -','FontWeight','bold');
lblRtt.Layout.Row = 4; lblRtt.Layout.Column = [3 4];

%% ================= LOG + COUNTERS =================
pLog = uipanel(gl,'Title','RX Log (unchanged lines)','FontWeight','bold');
pLog.Layout.Row = 2; pLog.Layout.Column = [1 2];
pLog.BackgroundColor = [0.97 0.97 0.97];
pLog.ForegroundColor = [0 0 0];

gLog = uigridlayout(pLog,[1 2]);
gLog.ColumnWidth = {'3x','1x'};
gLog.Padding = [8 8 8 8];
gLog.ColumnSpacing = 10;

txtLog = uitextarea(gLog,'Editable','off');
txtLog.FontName = 'Consolas';
txtLog.FontSize = 12;
txtLog.BackgroundColor = [0.2 0.2 0.2];
txtLog.FontColor = [1 1 1];

pStats = uipanel(gLog,'Title','Counters','FontWeight','bold');
pStats.BackgroundColor = [0.97 0.97 0.97];
pStats.ForegroundColor = [0 0 0];

gStats = uigridlayout(pStats,[8 1]);
gStats.RowHeight = {30 30 30 30 30 30 30 30};
gStats.Padding = [8 8 8 8];

lblStatusCnt = uilabel(gStats,'Text','STATUS RX: 0');
lblFaultCnt  = uilabel(gStats,'Text','FAULT RX : 0');
lblAckCnt    = uilabel(gStats,'Text','ACK RX   : 0');
lblTargetTot = uilabel(gStats,'Text','Target Total: 0');
lblRemain    = uilabel(gStats,'Text','Remaining: 0');
lblLastFault = uilabel(gStats,'Text','Last Fault: -');
lblCong      = uilabel(gStats,'Text','Congestion: -');
lblLimitIR   = uilabel(gStats,'Text','Limit/IR: -');

% Make all labels black
allLabels = findall(fig,'Type','uilabel');
for k = 1:numel(allLabels)
    allLabels(k).FontColor = [0 0 0];
end

%% ================= CONNECT TCP =================
appendLog("Connecting to Jetson " + JETSON_IP + ":" + JETSON_PORT + " ...");
try
    c = tcpclient(JETSON_IP, JETSON_PORT, "Timeout", 10);
    lampConn.Color = [0.12 0.60 0.25];
    lblConn.Text = "Connected";
    appendLog("CONNECTED");
catch ME
    appendLog("ERROR: Connection failed. Start Python bridge on Jetson.");
    appendLog("Details: " + string(ME.message));
    return;
end

pause(0.3);
if c.NumBytesAvailable > 0
    read(c, c.NumBytesAvailable, "uint8");
end

%% ================= STATE ==================
buf = "";
rxStatus = 0; rxFault = 0; rxAck = 0;
targetTotal = 0;
lastItems = 0;

%% ================= TIMER LOOP =================
t = timer('ExecutionMode','fixedSpacing','Period',0.05,'TimerFcn',@onTick);
start(t);

% Existing buttons behavior (unchanged)
btnStart.ButtonPushedFcn       = @(~,~) sendCmd("START", []);
btnStop.ButtonPushedFcn        = @(~,~) sendCmd("STOP", []);
btnHome.ButtonPushedFcn        = @(~,~) sendCmd("HOME", []);
btnSendPwm.ButtonPushedFcn     = @(~,~) sendCmd("PWM", editPwm.Value);
btnAddTarget.ButtonPushedFcn   = @(~,~) addTarget();

% +/- buttons
btnPwmPlus.ButtonPushedFcn     = @(~,~) adjustPWM(+20);
btnPwmMinus.ButtonPushedFcn    = @(~,~) adjustPWM(-20);
btnTarPlus.ButtonPushedFcn     = @(~,~) adjustTarget(+1);
btnTarMinus.ButtonPushedFcn    = @(~,~) adjustTarget(-1);

% Slider: sync to editPwm (does NOT auto-send)
pwmSlider.ValueChangedFcn      = @(~,~) onSliderChanged();

fig.CloseRequestFcn = @onClose;

%% ================= FUNCTIONS =================

function onSliderChanged()
    v = round(pwmSlider.Value);
    editPwm.Value = v;
end

function adjustPWM(step)
    v = editPwm.Value;
    if isempty(v), v = 255; end
    v = v + step;
    if v > 255, v = 255; end
    if v < 0, v = 0; end
    editPwm.Value = v;
    pwmSlider.Value = v;
end

function adjustTarget(step)
    v = editTarget.Value;
    if isempty(v), v = 0; end
    v = v + step;
    if v < 0, v = 0; end
    editTarget.Value = v;
end

function onTick(~,~)
    if ~isvalid(fig); return; end
    try
        if c.NumBytesAvailable > 0
            raw = read(c, c.NumBytesAvailable, "uint8");
            buf = buf + string(char(raw(:)'));

            while contains(buf, newline)
                [line, buf] = strtok(buf, newline);
                line = strtrim(line);
                if line == ""; continue; end

                appendLog("RX: " + line);

                try
                    msg = jsondecode(char(line));
                catch
                    continue
                end
                if ~isfield(msg,"type"); continue; end

                switch string(msg.type)
                    case "STATUS"
                        rxStatus = rxStatus + 1;
                        lblStatusCnt.Text = "STATUS RX: " + rxStatus;

                        if isfield(msg,'state')
                            st = string(msg.state);
                            lblState.Text = "State: " + st;

                            if st == "STOPPED" || st == "DONE"
                                lampRun.Color = [0.75 0.15 0.15];
                                lblRun.Text = char(st);
                            else
                                lampRun.Color = [0.12 0.60 0.25];
                                lblRun.Text = "Running";
                            end
                        end
                        if isfield(msg,'distance_cm'); lblDist.Text = "Distance (cm): " + num2str(msg.distance_cm,'%.2f'); end
                        if isfield(msg,'speed_rpm');    lblRpm.Text  = "Speed (rpm): " + string(msg.speed_rpm); end
                        if isfield(msg,'items')
                            lblItems.Text = "Items: " + string(msg.items);
                            lastItems = double(msg.items);
                        end
                        if isfield(msg,'interval');     lblInt.Text  = "Interval (ms): " + string(msg.interval); end
                        if isfield(msg,'rtt');          lblRtt.Text  = "RTT (ms): " + string(msg.rtt); end

                        if isfield(msg,'congestion')
                            if double(msg.congestion)==1, lblCong.Text="Congestion: YES"; else, lblCong.Text="Congestion: NO"; end
                        end
                        if isfield(msg,'limit') && isfield(msg,'ir')
                            lblLimitIR.Text = "Limit/IR: " + string(msg.limit) + " / " + string(msg.ir);
                        end

                        remain = max(0, targetTotal - lastItems);
                        lblRemain.Text = "Remaining: " + remain;

                    case "FAULT"
                        rxFault = rxFault + 1;
                        lblFaultCnt.Text = "FAULT RX : " + rxFault;
                        if isfield(msg,'code')
                            lblLastFault.Text = "Last Fault: " + string(msg.code);
                        else
                            lblLastFault.Text = "Last Fault: (unknown)";
                        end

                    case "ACK"
                        rxAck = rxAck + 1;
                        lblAckCnt.Text = "ACK RX   : " + rxAck;
                end
            end
        end
    catch
    end
end

function sendCmd(cmd, value)
    try
        if isempty(value)
            s = sprintf('{"type":"CMD","cmd":"%s"}\n', char(cmd));
        else
            s = sprintf('{"type":"CMD","cmd":"%s","value":%d}\n', char(cmd), round(value));
        end
        write(c, uint8(s));
        appendLog("TX: " + strtrim(string(s)));
    catch ME
        appendLog("TX ERROR: " + string(ME.message));
    end
end

function addTarget()
    v = editTarget.Value;
    if isempty(v) || v <= 0
        return;
    end

    targetTotal = targetTotal + round(v);
    lblTargetTot.Text = "Target Total: " + targetTotal;

    % send target increment to ESP
    sendCmd("TARGET", round(v));

    remain = max(0, targetTotal - lastItems);
    lblRemain.Text = "Remaining: " + remain;

    appendLog("Target updated: +" + round(v) + " (Total=" + targetTotal + ")");
end

function appendLog(s)
    L = txtLog.Value;
    L{end+1} = char(s);
    if numel(L) > 500
        L = L(end-499:end);
    end
    txtLog.Value = L;
    drawnow limitrate;
end

function onClose(~,~)
    try
        stop(t); delete(t);
    catch
    end
    try
        clear c
    catch
    end
    delete(fig);
end

end
