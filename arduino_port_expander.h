// Must disable logging if using logging in main.cpp or in other custom components for the
//  __c causes a section type conflict with __c thingy
// you can enable logging and use it if you enable this in logger:
/*
logger:
  level: DEBUG
  esp8266_store_log_strings_in_flash: False
  */

#define APE_TEXT_SENSOR
//#define APE_LOGGING

// take advantage of LOG_ defines to decide which code to include
#ifdef LOG_BINARY_OUTPUT
#define APE_BINARY_OUTPUT
#endif
#ifdef LOG_BINARY_SENSOR
#define APE_BINARY_SENSOR
#endif
#ifdef LOG_SENSOR
#define APE_SENSOR
#endif

static const char *TAGape = "ape";

#define APE_CMD_DIGITAL_READ 0
#define APE_CMD_WRITE_ANALOG 2
#define APE_CMD_WRITE_DIGITAL_HIGH 3
#define APE_CMD_WRITE_DIGITAL_LOW 4
#define APE_CMD_SETUP_PIN_OUTPUT 5
#define APE_CMD_SETUP_PIN_INPUT_PULLUP 6
#define APE_CMD_SETUP_PIN_INPUT 7
// 16 analog registers.. A0 to A15
// A4 and A5 on Arduino Uno not supported due to I2C
#define CMD_ANALOG_READ_A0 0b1000 // 0x8 = A0
// ....
#define CMD_ANALOG_READ_A15 10111 // 17 = A15

#define CMD_SETUP_ANALOG_INTERNAL 0x10
#define CMD_SETUP_ANALOG_DEFAULT 0x11

#define CMD_READ_TEXT 42

#define get_ape(constructor) static_cast<ArduinoPortExpander *>(constructor.get_component(0))

#define ape_binary_output(ape, pin) get_ape(ape)->get_binary_output(pin)
#define ape_binary_sensor(ape, pin) get_ape(ape)->get_binary_sensor(pin)
#define ape_analog_input(ape, pin) get_ape(ape)->get_analog_input(pin)
#define ape_text_sensor(ape, id, name) get_ape(ape)->get_text_sensor(id, name)

class ArduinoPortExpander;

using namespace esphome;

#ifdef APE_BINARY_OUTPUT
class ApeBinaryOutput : public output::BinaryOutput
{
public:
  ApeBinaryOutput(ArduinoPortExpander *parent, uint8_t pin)
  {
    this->parent_ = parent;
    this->pin_ = pin;
  }
  void write_state(bool state) override;
  uint8_t get_pin() { return this->pin_; }

protected:
  ArduinoPortExpander *parent_;
  uint8_t pin_;
  // Pins are setup as output after the state is written, Arduino has no open drain outputs, after setting an output it will either sink or source thus activating outputs writen to false during a flick.
  bool setup_{true};
  bool state_{false};

  friend class ArduinoPortExpander;
};
#endif

#ifdef APE_BINARY_SENSOR
class ApeBinarySensor : public binary_sensor::BinarySensor
{
public:
  ApeBinarySensor(ArduinoPortExpander *parent, uint8_t pin)
  {
    this->pin_ = pin;
  }
  uint8_t get_pin() { return this->pin_; }

protected:
  uint8_t pin_;
};
#endif

#ifdef APE_SENSOR
class ApeAnalogInput : public sensor::Sensor
{
public:
  ApeAnalogInput(ArduinoPortExpander *parent, uint8_t pin)
  {
    this->pin_ = pin;
  }
  uint8_t get_pin() { return this->pin_; }

protected:
  uint8_t pin_;
};
#endif

#ifdef APE_TEXT_SENSOR
class ApeTextSensor : public text_sensor::TextSensor
{
public:
  ApeTextSensor(ArduinoPortExpander *parent, uint8_t sensorId, const std::string name) : text_sensor::TextSensor(name)
  {
    this->sensorId_ = sensorId;
  }
  uint8_t get_sensorId() { return this->sensorId_; }

protected:
  uint8_t sensorId_;
};
#endif

class ArduinoPortExpander : public Component, public I2CDevice
{
public:
  ArduinoPortExpander(I2CBus *bus, uint8_t address, bool vref_default = false)
  {
    set_i2c_address(address);
    set_i2c_bus(bus);
    this->vref_default_ = vref_default;
  }
  void setup() override
  {
#ifdef APE_LOGGING
    ESP_LOGCONFIG(TAGape, "Setting up ArduinoPortExpander at %#02x ...", address_);
#endif

    /* We cannot setup as usual as arduino boots later than esp8266

            Poll i2c bus for our Arduino for a n seconds instead of failing fast,
            also this is important as pin setup (INPUT_PULLUP, OUTPUT it's done once)
        */
    this->configure_timeout_ = millis() + 5000;
  }
  unsigned long previousMillis = 0;
  unsigned long updateInterval = 5000;
  void loop() override
  {
    if (millis() < this->configure_timeout_)
    {
      bool try_configure = millis() % 100 > 50;
      if (try_configure == this->configure_)
        return;
      this->configure_ = try_configure;

      if (ERROR_OK == this->read_register(APE_CMD_DIGITAL_READ, const_cast<uint8_t *>(this->read_buffer_), 9)) //changed 3 to 9
      {
#ifdef APE_LOGGING
        ESP_LOGCONFIG(TAGape, "ArduinoPortExpander found at %#02x", address_);
#endif
        delay(10);
        if (this->vref_default_)
        {
          this->write_register(CMD_SETUP_ANALOG_DEFAULT, nullptr, 0); // 0: unused
        }

        // Config success
        this->configure_timeout_ = 0;
        this->status_clear_error();
#ifdef APE_BINARY_SENSOR
        for (ApeBinarySensor *pin : this->input_pins_)
        {
          App.feed_wdt();
          uint8_t pinNo = pin->get_pin();
#ifdef APE_LOGGING
          ESP_LOGCONFIG(TAGape, "Setup input pin %d", pinNo);
#endif
          this->write_register(APE_CMD_SETUP_PIN_INPUT_PULLUP, &pinNo, 1);
          delay(20);
        }
#endif
#ifdef APE_BINARY_OUTPUT
        for (ApeBinaryOutput *output : this->output_pins_)
        {
          if (!output->setup_)
          { // this output has a valid value already
            this->write_state(output->pin_, output->state_, true);
            App.feed_wdt();
            delay(20);
          }
        }
#endif
#ifdef APE_SENSOR
        for (ApeAnalogInput *sensor : this->analog_pins_)
        {
          App.feed_wdt();
          uint8_t pinNo = sensor->get_pin();
#ifdef APE_LOGGING
          ESP_LOGCONFIG(TAGape, "Setup analog input pin %d", pinNo);
#endif
          this->write_register(APE_CMD_SETUP_PIN_INPUT, &pinNo, 1);
          delay(20);
        }
#endif
        return;
      }
      // Still not answering
      return;
    }
    if (this->configure_timeout_ != 0 && millis() > this->configure_timeout_)
    {
#ifdef APE_LOGGING
      ESP_LOGE(TAGape, "ArduinoPortExpander NOT found at %#02x", address_);
#endif
      this->mark_failed();
      return;
    }

#ifdef APE_BINARY_SENSOR
    if (ERROR_OK != this->read_register(APE_CMD_DIGITAL_READ, const_cast<uint8_t *>(this->read_buffer_), 9)) //Changed this from 3 to 9
    {
#ifdef APE_LOGGING
      ESP_LOGE(TAGape, "Error reading. Reconfiguring pending.");
#endif
      this->status_set_error();
      this->configure_timeout_ = millis() + 5000;
      return;
    }
    for (ApeBinarySensor *pin : this->input_pins_)
    {
      uint8_t pinNo = pin->get_pin();

      uint8_t bit = pinNo % 8;

      uint8_t value = 0;
      if(pinNo < 8){
        value = this->read_buffer_[0];
      }else if(pinNo < 16){
        value = this->read_buffer_[1];
      }else if(pinNo < 24){
        value = this->read_buffer_[2];
      }else if(pinNo < 32){
        value = this->read_buffer_[3];
      }else if(pinNo < 40){
        value = this->read_buffer_[4];
      }else if(pinNo < 48){
        value = this->read_buffer_[5];
      }else if(pinNo < 56){
        value = this->read_buffer_[6];
      }else if(pinNo < 64){
        value = this->read_buffer_[7];
      }else{
        value = this->read_buffer_[8];
      }
      
      bool ret = value & (1 << bit);
      if (this->initial_state_)
        pin->publish_initial_state(ret);
      else
        pin->publish_state(ret);
    }
#endif
#ifdef APE_SENSOR
    for (ApeAnalogInput *pin : this->analog_pins_)
    {
      uint8_t pinNo = pin->get_pin();
      pin->publish_state(analogRead(pinNo));
    }
#endif
#ifdef APE_TEXT_SENSOR
// send a request for each text sensor to the arduino every x number of seconds
if(previousMillis == 0 || (millis() - previousMillis >= updateInterval)) {
    previousMillis = millis();
    for (ApeTextSensor *ts : this->text_sensors_)
    {
      uint8_t sensorId = ts->get_sensorId();
#ifdef APE_LOGGING
      ESP_LOGD(TAGape, "Reading Text Sensor. %d", sensorId);
#endif
      ts->publish_state(textRead(sensorId));
    }
}
#endif

    this->initial_state_ = false;
  }

#ifdef APE_SENSOR
  uint16_t analogRead(uint8_t pin)
  {
    bool ok = (ERROR_OK == this->read_register((uint8_t)(CMD_ANALOG_READ_A0 + pin), const_cast<uint8_t *>(this->read_buffer_), 2));
#ifdef APE_LOGGING
    ESP_LOGVV(TAGape, "analog read pin: %d ok: %d byte0: %d byte1: %d", pin, ok, this->read_buffer_[0], this->read_buffer_[1]);
#endif
    uint16_t value = this->read_buffer_[0] | ((uint16_t)this->read_buffer_[1] << 8);
    return value;
  }
#endif

#ifdef APE_TEXT_SENSOR
  std::string textRead(uint8_t id)
  {
    bool ok = (ERROR_OK == this->read_register((uint8_t)(CMD_READ_TEXT + id), const_cast<uint8_t *>(this->read_buffer_), 9));
 #ifdef APE_LOGGING
    //ESP_LOGVV(TAGape, "text read id: %d ok: %d byte0: %d byte1: %d", pin, ok, this->read_buffer_[0], this->read_buffer_[1]);
 #endif
    std::string result(reinterpret_cast<char const*>(&this->read_buffer_), sizeof(this->read_buffer_));
    return result;
  }
#endif


#ifdef APE_BINARY_OUTPUT
  output::BinaryOutput *get_binary_output(uint8_t pin)
  {
    ApeBinaryOutput *output = new ApeBinaryOutput(this, pin);
    output_pins_.push_back(output);
    return output;
  }
#endif
#ifdef APE_BINARY_SENSOR
  binary_sensor::BinarySensor *get_binary_sensor(uint8_t pin)
  {
    ApeBinarySensor *binarySensor = new ApeBinarySensor(this, pin);
    input_pins_.push_back(binarySensor);
    return binarySensor;
  }
#endif
#ifdef APE_SENSOR
  sensor::Sensor *get_analog_input(uint8_t pin)
  {
    ApeAnalogInput *input = new ApeAnalogInput(this, pin);
    analog_pins_.push_back(input);
    return input;
  }
#endif
#ifdef APE_TEXT_SENSOR
  text_sensor::TextSensor *get_text_sensor(uint8_t id, const std::string name)
  {
    ApeTextSensor *sensor = new ApeTextSensor(this, id, name);
    text_sensors_.push_back(sensor);
    return sensor;
  }
#endif
  void write_state(uint8_t pin, bool state, bool setup = false)
  {
    if (this->configure_timeout_ != 0)
      return;
#ifdef APE_LOGGING
    ESP_LOGD(TAGape, "Writing %d to pin %d", state, pin);
#endif
    this->write_register(state ? APE_CMD_WRITE_DIGITAL_HIGH : APE_CMD_WRITE_DIGITAL_LOW, &pin, 1);
    if (setup)
    {
      App.feed_wdt();
      delay(20);
#ifdef APE_LOGGING
      ESP_LOGI(TAGape, "Setup output pin %d", pin);
#endif
      this->write_register(APE_CMD_SETUP_PIN_OUTPUT, &pin, 1);
    }
  }

protected:
  bool configure_{true};
  bool initial_state_{true};
  uint8_t read_buffer_[9]{0, 0, 0, 0, 0, 0, 0, 0, 0}; //changed from [3]{0, 0, 0}
  unsigned long configure_timeout_{5000};
  bool vref_default_{false};

#ifdef APE_BINARY_OUTPUT
  std::vector<ApeBinaryOutput *> output_pins_;
#endif
#ifdef APE_BINARY_SENSOR
  std::vector<ApeBinarySensor *> input_pins_;
#endif
#ifdef APE_SENSOR
  std::vector<ApeAnalogInput *> analog_pins_;
#endif
#ifdef APE_TEXT_SENSOR
  std::vector<ApeTextSensor *> text_sensors_;
#endif 
};

#ifdef APE_BINARY_OUTPUT
void ApeBinaryOutput::write_state(bool state)
{
  this->state_ = state;
  this->parent_->write_state(this->pin_, state, this->setup_);
  this->setup_ = false;
}
#endif
