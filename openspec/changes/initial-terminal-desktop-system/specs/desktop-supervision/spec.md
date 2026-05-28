## ADDED Requirements

### Requirement: Wi-Fi data reception
The desktop application SHALL receive real-time telemetry from the terminal over Wi-Fi.

#### Scenario: Telemetry message arrives
- **WHEN** the terminal sends a telemetry JSON message
- **THEN** the desktop application SHALL parse the message
- **AND** update the latest terminal state

### Requirement: Real-time visualization
The desktop application SHALL display real-time curves and current system state.

#### Scenario: Valid telemetry parsed
- **WHEN** a valid telemetry message is parsed
- **THEN** the desktop application SHALL update temperature curves, estimated current, overload probability, and alarm state display

### Requirement: Parameter configuration
The desktop application SHALL allow users to configure terminal runtime parameters.

#### Scenario: User submits configuration
- **WHEN** the user updates sampling interval or alarm threshold
- **THEN** the desktop application SHALL send a configuration JSON message to the terminal

### Requirement: CSV data recording
The desktop application SHALL save received telemetry to CSV when recording is enabled.

#### Scenario: Recording enabled
- **WHEN** recording is enabled and telemetry arrives
- **THEN** the desktop application SHALL append the telemetry fields to a CSV file

### Requirement: Connection fault handling
The desktop application SHALL indicate terminal connection loss.

#### Scenario: Telemetry timeout
- **WHEN** no telemetry is received within the configured timeout
- **THEN** the desktop application SHALL mark the terminal connection as disconnected
