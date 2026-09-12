#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <RF24.h>
#include <ESPressio_IRadio.hpp>

namespace ESPressio::NRF24 {

inline Radio::RadioAddress DefaultNRF24BroadcastAddress() noexcept {
    static constexpr std::uint8_t bytes[5] = {0xD2, 0xF0, 0xA5, 0x5A, 0xC3};
    return Radio::RadioAddress::FromBytes(bytes, 5);
}

/// <summary>Configuration for an nRF24L01/nRF24L01+ managed Radio provider.</summary>
struct NRF24RadioConfiguration {
    std::uint16_t CePin = 0;
    std::uint16_t CsnPin = 0;
    Radio::RadioAddress LocalAddress{};
    Radio::RadioAddress BroadcastAddress = DefaultNRF24BroadcastAddress();
    std::uint8_t Channel = 76;
    rf24_datarate_e DataRate = RF24_1MBPS;
    rf24_pa_dbm_e PowerLevel = RF24_PA_LOW;
    std::uint8_t RetryDelay = 5;
    std::uint8_t RetryCount = 15;
    /// <summary>Optional explicit physical contention-domain identity. Zero derives a deterministic identity from channel.</summary>
    Radio::RadioContentionDomainId ContentionDomain{};
};

/// <summary>nRF24L01/nRF24L01+ concrete implementing the finite managed ESPressio-Radio provider contract.</summary>
/// <remarks>
/// RF24::write is synchronous. Successful unicast therefore proves TransmissionCompletion plus peer acknowledgement;
/// broadcast proves TransmissionCompletion with acknowledgement unavailable. RX has no provider-proximate timestamp in
/// the RF24 API used here, so timestamp evidence is explicitly Unknown/Unbounded and this provider cannot certify K1/K2
/// Clock synchronization without a stronger platform capture source.
/// </remarks>
class NRF24Radio final : public Radio::IRadio {
private:
    static constexpr std::uint8_t AddressBytes = 5;
    static constexpr std::uint8_t MaximumPayloadBytes = 32;
    static constexpr std::uint16_t MaximumLogicalTransferBytes = 3060; // (32 - (15 + 5)) * 255
    static constexpr std::size_t HardwareReceiveFifoPackets = 3;

    NRF24RadioConfiguration _configuration;
    RF24 _radio;
    Radio::IRadioReceiver* _receiver = nullptr;
    Radio::IRadioRuntimeSink* _runtimeSink = nullptr;
    bool _started = false;
    std::uint32_t _lifecycleGeneration = 0;

    bool ValidateAddress(const Radio::RadioAddress& address) const noexcept {
        return address.IsValid() && address.Length == AddressBytes;
    }

    Radio::RadioContentionDomainId EffectiveContentionDomain() const noexcept {
        if (_configuration.ContentionDomain) return _configuration.ContentionDomain;
        return {static_cast<std::uint32_t>(0x4E240100u + _configuration.Channel)};
    }

    std::uint64_t DataRateBitsPerSecond() const noexcept {
        switch (_configuration.DataRate) {
            case RF24_250KBPS: return 250'000ULL;
            case RF24_2MBPS: return 2'000'000ULL;
            case RF24_1MBPS:
            default: return 1'000'000ULL;
        }
    }

public:
    explicit NRF24Radio(NRF24RadioConfiguration configuration)
        : _configuration(configuration), _radio(configuration.CePin, configuration.CsnPin) {}

    bool Start() override {
        if (_started) return true;
        if (!ValidateAddress(_configuration.LocalAddress) || !ValidateAddress(_configuration.BroadcastAddress) ||
            !EffectiveContentionDomain()) return false;
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
        ++_lifecycleGeneration;
        if (_lifecycleGeneration == 0) ++_lifecycleGeneration;
        if (_runtimeSink) _runtimeSink->LifecycleAvailabilityChanged(*this);
        return true;
    }

    void Stop() noexcept override {
        if (!_started) return;
        _radio.stopListening();
        _radio.powerDown();
        _started = false;
        if (_runtimeSink) _runtimeSink->LifecycleAvailabilityChanged(*this);
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
    Radio::RadioContentionDomainId ContentionDomain() const noexcept override { return EffectiveContentionDomain(); }

    Radio::RadioProviderResourceProfile ProviderResources() const noexcept override {
        return {
            static_cast<std::uint16_t>(HardwareReceiveFifoPackets),
            static_cast<std::uint16_t>(HardwareReceiveFifoPackets),
            0,
            _configuration.RetryCount
        };
    }

    bool IsTransmitReady() const noexcept override { return _started; }

    Radio::RadioTransmissionCost EstimateTransmissionCost(
        const Radio::RadioAddress&,
        std::size_t payloadBytes,
        const Radio::RadioServiceProfile&) const noexcept override {
        if (payloadBytes > MaximumPayloadBytes) return {};
        const std::uint64_t attempts = static_cast<std::uint64_t>(_configuration.RetryCount) + 1ULL;
        // Conservative physical bits: preamble/address/control/CRC allowance plus dynamic payload.
        const std::uint64_t bitsPerAttempt = static_cast<std::uint64_t>(payloadBytes + 16U) * 8ULL;
        const auto rate = DataRateBitsPerSecond();
        const std::uint64_t airPerAttempt = (bitsPerAttempt * 1'000'000'000ULL + rate - 1ULL) / rate;
        const std::uint64_t retryDelay = static_cast<std::uint64_t>(_configuration.RetryDelay + 1U) * 250'000ULL;
        const std::uint64_t retryGaps = _configuration.RetryCount == 0
            ? 0ULL : static_cast<std::uint64_t>(_configuration.RetryCount) * retryDelay;
        return {
            attempts * (payloadBytes + 16U),
            attempts * airPerAttempt + retryGaps,
            Radio::RadioCostEstimateQuality::ConservativeAirtime
        };
    }

    Radio::RadioSendResult Send(
        const Radio::RadioAddress& destination,
        const std::uint8_t* payload,
        std::size_t payloadSize) noexcept override {
        if (!_started) return {Radio::RadioSendStatus::NotStarted, 0};
        if (!ValidateAddress(destination)) return {Radio::RadioSendStatus::InvalidAddress, 0};
        if ((payload == nullptr && payloadSize != 0) || payloadSize > MaximumPayloadBytes)
            return {Radio::RadioSendStatus::PayloadTooLarge, 0};

        const bool broadcast = destination == _configuration.BroadcastAddress || destination.IsBroadcast();
        const Radio::RadioAddress& txAddress = destination.IsBroadcast() ? _configuration.BroadcastAddress : destination;
        _radio.stopListening(txAddress.Bytes.data());
        const bool delivered = _radio.write(payload, static_cast<std::uint8_t>(payloadSize), broadcast);
        _radio.startListening();
        if (!delivered) return {Radio::RadioSendStatus::NativeFailure, 0};

        return Radio::RadioSendResult::Accepted(
            broadcast
                ? Radio::RadioDirectLinkEvidence::CompletedWithoutPeerAcknowledgement()
                : Radio::RadioDirectLinkEvidence::CompletedAndAcknowledged());
    }

    void SetReceiver(Radio::IRadioReceiver* receiver) noexcept override { _receiver = receiver; }
    void SetRuntimeSink(Radio::IRadioRuntimeSink* sink) noexcept override { _runtimeSink = sink; }

    Radio::ManagedRadioIngressServiceResult ServiceInbound(std::size_t maximumPackets = 0U) noexcept override {
        Radio::ManagedRadioIngressServiceResult result{};
        if (!_started) return result;
        const std::size_t limit = maximumPackets == 0U
            ? HardwareReceiveFifoPackets : (maximumPackets < HardwareReceiveFifoPackets ? maximumPackets : HardwareReceiveFifoPackets);
        std::uint8_t pipe = 0;
        while (result.PacketsProcessed < limit && _radio.available(&pipe)) {
            const std::uint8_t length = _radio.getDynamicPayloadSize();
            if (length == 0 || length > MaximumPayloadBytes) {
                _radio.flush_rx();
                ++result.PacketsProcessed;
                continue;
            }
            std::array<std::uint8_t, MaximumPayloadBytes> payload{};
            _radio.read(payload.data(), length);
            Radio::RadioPacketView packet{};
            packet.Source = {};
            packet.Destination = pipe == 2 ? _configuration.BroadcastAddress : _configuration.LocalAddress;
            packet.Payload = payload.data();
            packet.PayloadSize = length;
            packet.Flags = pipe == 2 ? Radio::RadioPacketFlag::Broadcast : Radio::RadioPacketFlag::LinkAcknowledged;
            const Radio::RadioReceiveTimestampEvidence timestamp{
                0, 0, 0, _lifecycleGeneration,
                Radio::RadioTimestampCaptureSource::ServiceContext,
                Radio::RadioTimestampQuality::Unbounded};
            if (_receiver) _receiver->OnRadioPacket(*this, packet, timestamp);
            ++result.PacketsProcessed;
        }
        result.WorkRemaining = _radio.available(&pipe);
        if (result.WorkRemaining && _runtimeSink) _runtimeSink->InboundAvailable(*this);
        return result;
    }
};

} // namespace ESPressio::NRF24
