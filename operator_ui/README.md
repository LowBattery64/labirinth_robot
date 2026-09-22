# OmegaBot Operator Console

Qt 6 desktop interface for the OmegaBot operator.

Current behavior:
- Live video from Raspberry Pi TCP port 5001.
- Robot control and telemetry from TCP port 5000.
- W/A/S/D and arrow keys send persistent commands.
- Releasing a key does not stop the robot.
- S, Space and STOP send the stop command.
- Ultrasonic safety state is shown compactly.
- Detailed telemetry is not displayed on the main screen.
- Operator log is available on demand.

Build with Qt 6 Widgets and Network:
cmake -S . -B build
cmake --build build --config Release

Raspberry Pi: 10.109.150.232
Control/telemetry: TCP 5000
Video: TCP 5001
