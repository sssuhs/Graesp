## ADDED Requirements

### Requirement: Telemetry JSON schema
The terminal SHALL send telemetry as JSON containing temperatures, thermal features, estimated current, overload probability, state, and battery voltage.

#### Scenario: Telemetry generated
- **WHEN** the terminal completes one monitoring cycle
- **THEN** it SHALL send a JSON message with type, timestamp, NTC temperatures, thermal features, estimated current, overload probability, state, and battery voltage

### Requirement: Configuration JSON schema
The desktop application SHALL send configuration as JSON containing sampling and alarm parameters.

#### Scenario: Configuration sent
- **WHEN** the desktop application sends a configuration update
- **THEN** the JSON message SHALL include type and at least one configurable parameter

### Requirement: State values
The protocol SHALL use a fixed set of terminal state values.

#### Scenario: Terminal reports state
- **WHEN** telemetry includes state
- **THEN** state SHALL be one of normal, warning, overload, or sensor_error

### Requirement: Robust JSON parsing
Both terminal and desktop application SHALL ignore unknown JSON fields while preserving known-field behavior.

#### Scenario: Unknown field received
- **WHEN** a JSON message contains an unknown field
- **THEN** the receiver SHALL ignore the unknown field
- **AND** continue processing known fields if the message is otherwise valid
