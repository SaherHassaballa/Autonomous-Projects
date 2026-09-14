clear; clc;

port = 'COM5';  % ??? ?????? ??? ?????
baud = 115200;

% === Connect to Arduino Serial ===
s = serial(port,'BaudRate',baud);
fopen(s);

disp("Listening to Arduino...");

% === Plot setup ===
figure;
hold on;
grid on;

h1 = plot(nan, nan, 'r', 'LineWidth', 1.5); % Angle
h2 = plot(nan, nan, 'g', 'LineWidth', 1.5); % PID Output
h3 = plot(nan, nan, 'b', 'LineWidth', 1.5); % Speed

legend("Angle", "PID Output", "Speed");
xlabel("Samples");
ylabel("Value");
title("Self-Balance Robot Signals");

angleData = [];
pidData = [];
speedData = [];
i = 0;

% === Live Reading Loop ===
while true
    if s.BytesAvailable > 0
        rawLine = fgets(s);
        vals = str2double(strsplit(strtrim(rawLine), ","));
        
        if numel(vals) == 3
            i = i + 1;
            angleData(i) = vals(1);
            pidData(i)   = vals(2);
            speedData(i) = vals(3);
            
            set(h1, 'XData', 1:i, 'YData', angleData);
            set(h2, 'XData', 1:i, 'YData', pidData);
            set(h3, 'XData', 1:i, 'YData', speedData);
            
            drawnow limitrate;
        end
    end
end

% Cleanup if stopped manually
fclose(s);
delete(s);
clear s;
