#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <RF24.h>
#include <ESPressio_IRadio.hpp>

namespace ESPressio::NRF24 {

inline Radio::RadioAddress DefaultNRF24BroadcastAddress() noexcept {
    static constexpr uint8_t bytes[5] = {0xD2, 0xF0, 0xA5, 0x5A, 0xC3};
    return Radio::RadioAddress::FromBytes(bytes, 5);
}

/// <summary>Configuration for an nRF24L01/nRF24L01+ ESPressio radio provider.</summary>
/**
 * ESPressio Memory Audit
 * Members:
 * - CePin (uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - CsnPin (uint16_t): 2 bytes [0 bytes dynamic allocation]
 * - LocalAddress (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - BroadcastAddress (Radio::RadioAddress): 9 bytes [0 bytes dynamic allocation]
 * - Channel (uint8_t): 1 bytes [0 bytes dynamic allocation]
 * - DataRate (rf24_datarate_e): 4 bytes [0 bytes dynamic allocation]
 * - PowerLevel (rf24_pa_dbm_e): 4 bytes [0 bytes dynamic allocation]
 * - RetryDelay (uint8_t): 1 bytes [0 bytes dynamic allocation]
 * - RetryCount (uint8_t): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 36 bytes [0 bytes dynamic allocation]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: medium; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
struct NRF24RadioConfiguration {
    uint16_t CePin = 0;
    uint16_t CsnPin = 0;
    Radio::RadioAddress LocalAddress{};
    Radio::RadioAddress BroadcastAddress = DefaultNRF24BroadcastAddress();
    uint8_t Channel = 76;
    rf24_datarate_e DataRate = RF24_1MBPS;
    rf24_pa_dbm_e PowerLevel = RF24_PA_LOW;
    uint8_t RetryDelay = 5;
    uint8_t RetryCount = 15;
};

/// <summary>nRF24L01/nRF24L01+ concrete for ESPressio-Radio using the RF24 driver.</summary>
/// <remarks>
/// RF24::write is synchronous. A successful unicast write therefore proves link transmission completion and hardware
/// acknowledgement after the configured retry policy; broadcast/multicast writes prove transmission completion but have
/// no peer acknowledgement. This qualified evidence is returned separately from Radio send admission.
/// </remarks>
/**
 * ESPressio Memory Audit
 * Inherited Memory Total: 4 bytes [0 bytes dynamic allocation]
 * Members:
 * - _configuration (NRF24RadioConfiguration): 36 bytes [0 bytes dynamic allocation]
 * - _radio (RF24): sizeof(RF24) (target/toolchain dependent) [0 bytes dynamic allocation]
 * - _receiver (Radio::IRadioReceiver*): 4 bytes [0 bytes dynamic allocation]
 * - _workSignal (Radio::IRadioWorkSignal*): 4 bytes [0 bytes dynamic allocation]
 * - _observers (Radio::RadioObserverSubscriptions): 8 bytes [_dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * - _started (bool): 1 bytes [0 bytes dynamic allocation]
 * Total Memory: 57 bytes known/aligned storage + sizeof(RF24) (target/toolchain dependent) [_observers: _dispatcher: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 52 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: enable_shared_from_this: embedded weak_ptr shares a control block when activated; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: shared control block (~12+ bytes; allocate_shared may co-locate object) + object 20 bytes; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _mutex: native synchronization state may allocate platform resources lazily; _observers: _dispatcher: pointee: Observable: IUntypedObservable: IObservable: _lifetimeControl: pointee: _condition: native condition-variable state may allocate platform synchronization resources; _observers: _dispatcher: pointee: Observable: _registrations: Capacity * (12 bytes) element storage; _observers: _dispatcher: pointee: Observable: _bindings: Capacity * (12 bytes) element storage]
 * Basis: ESP32/Xtensa ILP32 reference ABI (4-byte pointers/size_t); ESPressio stateful allocators/deleters included; ABI-sensitive STL/platform internals are identified explicitly.
 * Confidence: low; compile-time sizeof on the concrete target remains authoritative for ABI-sensitive/opaque members.
 * End ESPressio Memory Audit
 */
class NRF24Radio final : public Radio::IRadio {
private:
    static constexpr uint8_t AddressBytes = 5;
    static constexpr uint8_t MaximumPayloadBytes = 32;
    static constexpr uint16_t MaximumLogicalTransferBytes = 4096;

    NRF24RadioConfiguration _configuration;
    RF24 _radio;
    Radio::IRadioReceiver* _receiver = nullptr;
    Radio::IRadioWorkSignal* _workSignal = nullptr;
    Radio::RadioObserverSubscriptions _observers{};
    bool _started = false;

    bool ValidateAddress(const Radio::RadioAddress& address) const noexcept {
        return address.IsValid() && address.Length == AddressBytes;
    }

public:
    explicit NRF24Radio(NRF24RadioConfiguration configuration)
        : _configuration(configuration), _radio(configuration.CePin, configuration.CsnPin) {}

    bool Start() override {
        if (_started) return true;
        if (!ValidateAddress(_configuration.LocalAddress) || !ValidateAddress(_configuration.BroadcastAddress)) return false;
        if (!_radio.begin()) return false;
        _radio.setAddressWidth(AddressBytes);
        _radio.setChannel(_configuration.Channel);
        if (!_radio.setDataRate(_configuration.DataRate)) return false;
        _radio.setPALevel(_configuration.PowerLevel);
        _radio.setRetries(_configuration.RetryDelay, _configuration.RetryCount);
        _radio.setAutoAck(true);
        _radio.enableDynamicPayloads();
        _radio.openReadingPipe(1, _configuration.LocalAddress.Bytes.data());
        _radio.openReadingPipe(2, _configuration.BroadcastAddress.Bytes.data());
        _radio.startListening();
        _started = true;
        _observers.NotifyStarted(*this);
        return true;
    }

    void Stop() noexcept override {
        if (!_started) return;
        _radio.stopListening();
        _radio.powerDown();
        _started = false;
        _observers.NotifyStopped(*this);
    }

    bool IsStarted() const noexcept override { return _started; }

    Radio::RadioCapabilities Capabilities() const noexcept override {
        return {
            Radio::RadioCapability::Broadcast |
            Radio::RadioCapability::LinkAcknowledgement |
            Radio::RadioCapability::LinkRetries |
            Radio::RadioCapability::ChannelSelection |
            Radio::RadioCapability::DataRateSelection |
            Radio::RadioCapability::TransmitPower |
            Radio::RadioCapability::HardwareAddressing,
            MaximumPayloadBytes,
            AddressBytes,
            MaximumLogicalTransferBytes
        };
    }

    Radio::RadioAddress LocalAddress() const noexcept override { return _configuration.LocalAddress; }

    Radio::RadioSendResult Send(
        const Radio::RadioAddress& destination,
        const uint8_t* payload,
        std::size_t payloadSize
    ) override {
        const auto complete = [&](Radio::RadioSendResult result) {
            _observers.NotifySendAttempted(*this, destination, payloadSize, result);
            return result;
        };
        if (!_started) return complete({Radio::RadioSendStatus::NotStarted, 0});
        if (!ValidateAddress(destination)) return complete({Radio::RadioSendStatus::InvalidAddress, 0});
        if ((payload == nullptr && payloadSize != 0) || payloadSize > MaximumPayloadBytes)
            return complete({Radio::RadioSendStatus::PayloadTooLarge, 0});

        const bool broadcast = destination == _configuration.BroadcastAddress || destination.IsBroadcast();
        const Radio::RadioAddress& txAddress = destination.IsBroadcast() ? _configuration.BroadcastAddress : destination;
        _radio.stopListening(txAddress.Bytes.data());
        const bool delivered = _radio.write(payload, static_cast<uint8_t>(payloadSize), broadcast);
        _radio.startListening();
        if (!delivered) return complete({Radio::RadioSendStatus::NativeFailure, 0});

        return complete(Radio::RadioSendResult::Accepted(
            broadcast
                ? Radio::RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement()
                : Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged()
        ));
    }

    void SetReceiver(Radio::IRadioReceiver* receiver) noexcept override { _receiver = receiver; }
    void SetWorkSignal(Radio::IRadioWorkSignal* signal) noexcept override { _workSignal = signal; }
    Radio::RadioObserverSubscriptions& Observers() noexcept override { return _observers; }

    void DrainInbound() override {
        if (!_started) return;
        uint8_t pipe = 0;
        while (_radio.available(&pipe)) {
            const uint8_t length = _radio.getDynamicPayloadSize();
            if (length == 0 || length > MaximumPayloadBytes) {
                _radio.flush_rx();
                continue;
            }
            std::array<uint8_t, MaximumPayloadBytes> payload{};
            _radio.read(payload.data(), length);

            Radio::RadioPacketView packet;
            packet.Source = {};
            packet.Destination = pipe == 2 ? _configuration.BroadcastAddress : _configuration.LocalAddress;
            packet.Payload = payload.data();
            packet.PayloadSize = length;
            packet.Flags = pipe == 2 ? Radio::RadioPacketFlag::Broadcast : Radio::RadioPacketFlag::LinkAcknowledged;
            if (_receiver != nullptr) _receiver->OnRadioPacket(*this, packet);
        }
    }
};

} // namespace ESPressio::NRF24
