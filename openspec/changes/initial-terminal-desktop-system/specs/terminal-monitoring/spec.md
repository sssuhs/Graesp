## ADDED Requirements

### Requirement: Non-invasive terminal installation
The terminal SHALL monitor overload risk without entering the power strip enclosure, cutting conductors, stripping insulation, or inserting a current sensor into the terminal algorithm input path.

#### Scenario: Clamp installed on input cable
- **WHEN** the user installs the terminal on a power strip input cable
- **THEN** the sensing structure SHALL only contact or clamp the cable outer insulation
- **AND** the terminal SHALL not modify the original electrical structure of the power strip

### Requirement: Three-channel NTC acquisition
The terminal SHALL acquire three NTC temperature channels, with two channels assigned to cable-surface temperature and one channel assigned to ambient temperature.

#### Scenario: Periodic temperature sampling
- **WHEN** a sampling interval elapses
- **THEN** the terminal SHALL read three ADC channels
- **AND** convert the readings into Celsius temperature values

### Requirement: Thermal feature extraction
The terminal SHALL compute thermal features from the three NTC channels.

#### Scenario: Feature calculation after sampling
- **WHEN** the terminal obtains valid NTC temperatures
- **THEN** it SHALL compute cable average temperature, cable maximum temperature, ambient temperature, temperature rise, cable sensor difference, and temperature rise rate

### Requirement: Current estimation interface
The terminal SHALL expose an interface that converts thermal features into estimated current.

#### Scenario: Current estimation produced
- **WHEN** thermal features are updated
- **THEN** the terminal SHALL produce an estimated current value for telemetry and overload detection

### Requirement: Overload detection
The terminal SHALL classify the operating state as normal, warning, overload, or sensor error.

#### Scenario: Overload probability exceeds threshold
- **WHEN** the overload probability or rule result exceeds the configured overload threshold
- **THEN** the terminal SHALL set the state to overload
- **AND** activate local alarm output

### Requirement: Local alarm output
The terminal SHALL provide local alarm output using LED and buzzer.

#### Scenario: Warning state
- **WHEN** the terminal state is warning
- **THEN** the LED SHALL indicate warning
- **AND** the buzzer SHALL follow the configured warning policy

### Requirement: Battery-powered operation
The terminal SHALL support lithium-battery powered operation and expose battery voltage in telemetry.

#### Scenario: Battery voltage sampled
- **WHEN** telemetry is generated
- **THEN** the terminal SHALL include battery voltage in millivolts

### Requirement: Low-power behavior
The terminal SHALL reduce power consumption during normal periodic monitoring.

#### Scenario: Sampling cycle completes
- **WHEN** sampling, feature calculation, detection, alarm update, and telemetry send are complete
- **THEN** the terminal SHALL enter a low-duty or sleep-friendly waiting state until the next sampling cycle
