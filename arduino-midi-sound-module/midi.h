/*
    Serial driver & MIDI decoder
    https://github.com/DLehenbauer/arduino-midi-sound-module

    Receives, decodes, and dispatches MIDI data arriving via the Arduino Uno's serial USART.
    
    Sending MIDI data to the Arduino:
    
    The easiest/fastest way to send MIDI data from your computer is to use a MIDI <-> Serial Bridge:
      http://projectgus.github.io/hairless-midiserial/
      
    However, you will need to configure Hairless for 38400 baud to avoid running the buffer for
    incoming MIDI data.  (You will also need to adjust the baud rate passed to begin(..) as well.)
    
    If you have an ISP programmer and an Uno R3 w/ATMega82U, you can make your Arduino Uno appear
    as a native USB MIDI device:
      https://github.com/kuwatay/mocolufa

    When flashing the ATMega82U, the key on the ISP connector faces the pin sockets for [SCL..D8], which
    can be a tight fit (on one Uno, I had to bend the ISP pins slightly from the header to make room for
    the key to fit.)
    
                           .---.    
                           |o o|_  .-.
                           |o o _| |o| SCL
                           |o o|   |o| SDA
                           '---'   |o| AREF
                               1   |o| GND
                                   |o| 13
    
    After flashing 'mocolufa', the Arduino will appear as a 'arduino_midi' HID device.  To make the Arduino
    appear as a serial COM device again, set a jumper across pins 2/4 and reconnect power:
    
                                
                            # o    .-.
                            # o    |o| SCL
                            o o    |o| SDA
                               1   |o| AREF
                                   |o| GND
                                   |o| 13
    
    TinyUSB Drivers:
    https://learn.adafruit.com/usbtinyisp/drivers
    
    Testing the connection:
    
        > .\avrdude -c usbtiny -p m16u2

        avrdude.exe: AVR device initialized and ready to accept instructions

        Reading | ################################################## | 100% 0.04s

        avrdude.exe: Device signature = 0x1e9489 (probably m16u2)

        avrdude.exe: safemode: Fuses OK (E:F4, H:D9, L:FF)

        avrdude.exe done.  Thank you.
        
    Backing up factory firmware:
    
        > .\avrdude -c usbtiny -P usb -p m16u2 -b 19200 -U flash:r:backup_flash.hex:i

        avrdude.exe: AVR device initialized and ready to accept instructions

        Reading | ################################################## | 100% 0.03s

        avrdude.exe: Device signature = 0x1e9489 (probably m16u2)
        avrdude.exe: reading flash memory:

        Reading | ################################################## | 100% 12.63s

        avrdude.exe: writing output file "backup_flash.hex"

        avrdude.exe: safemode: Fuses OK (E:F4, H:D9, L:FF)

        avrdude.exe done.  Thank you.
        
    Installing new firmware:
    
        > .\avrdude.exe -c usbtiny -P usb -p m16u2 -b 19200 -U flash:w:dualMoco.hex

        avrdude.exe: AVR device initialized and ready to accept instructions

        Reading | ################################################## | 100% 0.02s

        avrdude.exe: Device signature = 0x1e9489 (probably m16u2)
        avrdude.exe: NOTE: "flash" memory has been specified, an erase cycle will be performed
        To disable this feature, specify the -D option.
        avrdude.exe: erasing chip
        avrdude.exe: reading input file "USBMidiKliK_dual.hex"
        avrdude.exe: writing flash (8644 bytes):

        Writing | ################################################## | 100% 10.34s

        avrdude.exe: 8644 bytes of flash written
        avrdude.exe: verifying flash memory against USBMidiKliK_dual.hex:
        avrdude.exe: load data flash data from input file USBMidiKliK_dual.hex:
        avrdude.exe: input file USBMidiKliK_dual.hex contains 8644 bytes
        avrdude.exe: reading on-chip flash data:

        Reading | ################################################## | 100% 6.69s

        avrdude.exe: verifying ...
        avrdude.exe: 8644 bytes of flash verified

        avrdude.exe: safemode: Fuses OK (E:F4, H:D9, L:FF)

        avrdude.exe done.  Thank you.
      
    Finally, with a bit more circuitry, you can add an 5-pin DIN serial MIDI input port to the
    Arduino and use a standard serial MIDI interface.
    
                220 
        .------^v^v^----------o-------.                      .----o--------------o----< +5v
        |                     |       |                      |    |              |
        |     .-----.         |  1    |      .--------.      |   === 100nF       /
        |    / 5-DIN \       _|_ N    o----1-|        |-6----'    |              \ 
        |   |  (back) |       ^  9    o----2-| H11L1* |-5---------o--< Gnd       / 280
        |   |o       o|      /_\ 1    |      |        |-4----.                   \
        |    \ o o o /        |  4    |      '--------'      |                   /
        |     /-----\         |       |                      |                   |
        |  4 /       \ 5      |       |                      '-------------------o----> RX
        '---'         '-------o-------'
        
    Notes:
        * H11L1 is a PC900 equivalent

*/

#ifndef __MIDI_H__
#define __MIDI_H__

#include <avr/interrupt.h>
#include <avr/io.h>
#include "ringbuffer.h"

extern void noteOn(uint8_t channel, uint8_t note, uint8_t velocity);
extern void noteOff(uint8_t channel, uint8_t note);
extern void controlChange(uint8_t channel, uint8_t data1, uint8_t data2);
extern void pitchBend(uint8_t channel, int16_t value);
extern void programChange(uint8_t channel, uint8_t program);
extern void sysex(uint8_t cbData, uint8_t bytes[]);

enum MidiStatus : uint8_t {
  /* 0x8n */ MidiStatus_NoteOff				    = 0,     // 2 data bytes
  /* 0x9n */ MidiStatus_NoteOn				    = 1,     // 2 data bytes
  /* 0xAn */ MidiStatus_PolyKeyPressure		= 2,     // 2 data bytes
  /* 0xBn */ MidiStatus_ControlChange			= 3,     // 2 data bytes
  /* 0xCn */ MidiStatus_ProgramChange			= 4,     // 1 data bytes
  /* 0xDn */ MidiStatus_ChannelPressure		= 5,     // 1 data bytes
  /* 0xEn */ MidiStatus_PitchBend				  = 6,     // 2 data bytes
  /* 0xFn */ MidiStatus_Extended				  = 7,     // (variable length)
  /* ???  */ MidiStatus_Unknown				    = 8      // (unknown)
};

class Midi final {
  private:
    static constexpr uint8_t maxMidiData = 32;
    static RingBuffer<uint8_t, /* Log2Capacity: */ 6> _midiBuffer;

    static constexpr int8_t midiStatusToDataLength[] = {
      /* 0x8n: MidiCommand_NoteOff               */ 2,
      /* 0x9n: MidiCommand_NoteOn                */ 2,
      /* 0xAn: MidiCommand_PolyKeyPressure       */ 2,
      /* 0xBn: MidiCommand_ControlChange         */ 2,
      /* 0xCn: MidiCommand_ProgramChange         */ 1,
      /* 0xDn: MidiCommand_ChannelPressure       */ 1,
      /* 0xEn: MidiCommand_PitchBend             */ 2,
      /* 0xFn: MidiCommand_Extended              */ maxMidiData
    };

    static MidiStatus midiStatus;			      // Status of the incoming message
    static uint8_t midiChannel;             // Channel of the incoming message
    static uint8_t midiDataRemaining;       // Expected number of data bytes remaining
    static uint8_t midiDataIndex;           // Location at which next data byte will be written
    static uint8_t midiData[maxMidiData];   // Buffer containing incoming data bytes
  
    static void dispatchCommand() {
      const uint8_t midiData0 = midiData[0];
    
      switch (midiStatus) {
        case MidiStatus_NoteOff: {
          noteOff(midiChannel, midiData0);
          break;
        }
        case MidiStatus_NoteOn: {
          if (midiData[1] == 0) {
            noteOff(midiChannel, midiData0);
            } else {
            noteOn(midiChannel, midiData0, midiData[1]);
          }
          break;
        }
        case MidiStatus_PitchBend: {
          int16_t value = midiData[1];
          value <<= 7;
          value |= midiData0;
          value -= 0x2000;
          pitchBend(midiChannel, value);
          break;
        }
        case MidiStatus_ControlChange: {
          controlChange(midiChannel, midiData0, midiData[1]);
          break;
        }
        case MidiStatus_ProgramChange: {
          programChange(midiChannel, midiData0);
          break;
        }

        default: { break; }
      }
    
      /* TODO: Handle running status?
      midiDataRemaining = midiStatusToDataLength[midiStatus];				// Running Status: reset the midi data buffer for the current midi status
      midiDataIndex = 0;
      */
    }

  public:
    // Configure the USART to begin receiving MIDI messages at the given 'baud'.
    // (Note: The messages are buffered until 'dispatch()' is called, usually in the main 'loop()'.)
    static void begin(uint32_t baud) {
      const uint16_t ubrr = F_CPU / 16 / baud - 1;

      UBRR0H = ubrr >> 8;                     // Baud
      UBRR0L = ubrr & 0xFF;

      UCSR0C = _BV(UCSZ01)  | _BV(UCSZ00);    // Async, 8n1 (8 data bits, parity none, 1 stop bit)
      UCSR0B |= _BV(RXEN0) | _BV(RXCIE0);     // Enable receive w/interrupt
    }

    // Called by the USART RX ISR to enqueue incoming MIDI bytes.  
    static void enqueue(uint8_t byte) {
      _midiBuffer.enqueue(byte);
    }

    // Called by 'dispatch()' to decode the next byte of a MIDI message.  The message is
    // dispatched tho the appropriate handler if it completes the current message.
    static void decode(uint8_t byte) {
      if (byte & 0x80) {													        // If the high bit is set, this is the start of a new message
        if (midiStatus == MidiStatus_Extended) {					//   If the previous status was an extended message (sysex or real-time)
          sysex(midiDataIndex, midiData);								  //     the next byte must be 0xF7 (i.e., EOX).  Ignore EOX and dispatch the sysex().
          midiStatus = MidiStatus_Unknown;							  //     The following byte must be a status byte beginning the next message.
          return;
        }
      
        midiStatus = static_cast<MidiStatus>((byte >> 4) - 8);
        midiDataRemaining = midiStatusToDataLength[midiStatus];			// Set the expected data bytes for the new message status.
        midiDataIndex = 0;												                  // Reset the midi data buffer.
        midiChannel = byte & 0x0F;
      } else {
        if (midiDataRemaining > 0) {					            // If more data bytes are expected for the current midi status
          midiData[midiDataIndex++] = byte;			          //	 then copy the next byte into the data buffer
          midiDataRemaining--;						                //	   and decrement the remaining data bytes expected.
          if (midiDataRemaining == 0) {				            //   If this was the last data byte expected
            dispatchCommand();						                //     then dispatch the current command.
          }
        }
      }
    }

    // Decode and dispatch all buffered MIDI messages.  Returns once the buffer is drained.
    static void dispatch() {
      uint8_t received;
      while (_midiBuffer.dequeue(received)) {
        decode(received);
      }
    }
};

MidiStatus Midi::midiStatus = MidiStatus_Unknown;     // Status of the incoming message
uint8_t Midi::midiChannel = 0xFF;                     // Channel of the incoming message
uint8_t Midi::midiDataRemaining = 0;                  // Expected number of data bytes remaining
uint8_t Midi::midiDataIndex = 0;                      // Location at which next data byte will be written
uint8_t Midi::midiData[maxMidiData] = { 0 };          // Buffer containing incoming data bytes
constexpr int8_t Midi::midiStatusToDataLength[];
RingBuffer<uint8_t, /* Log2Capacity: */ 6> Midi::_midiBuffer;

ISR(USART_RX_vect) {
  Midi::enqueue(UDR0);
}

#endif // __MIDI_H__