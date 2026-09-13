# ESPressio-NRF24

`ESPressio-NRF24` provides the nRF24L01/nRF24L01+ concrete `IRadio` implementation for `ESPressio-Radio`.

The provider is strictly a physical/link concrete. It transports opaque Radio packets, owns no Device or Mesh identity, performs no Mesh routing, and does not interpret Command, Event, State or any other Primitive family.

## Managed provider model

`NRF24Radio` implements the finite managed `IRadio` contract used by the Radio R3 contention-domain runtime:

- `ServiceInbound()` services at most the finite nRF24 hardware RX FIFO quantum (three packets by default).
- `ProviderResources()` publishes that finite ingress bound and the configured hardware retry count.
- `EstimateTransmissionCost()` reports conservative retry-aware airtime/fairness cost.
- `Send()` returns terminal direct-link evidence synchronously because `RF24::write()` itself is synchronous.
- successful unicast establishes both `TransmissionCompletion` and genuine peer acknowledgement;
- successful broadcast establishes `TransmissionCompletion` only and never invents peer acknowledgement;
- the provider has no hidden application worker, `RadioWorker`, family callback graph or provider-local fragmentation engine.

The Radio runtime owns scheduling, protected Q1 storage, transfer IDs, v3 fragmentation/reassembly, deadline treatment and logical-transfer terminal handoff.

## Physical and logical transfer bounds

The nRF24 physical payload ceiling is **32 bytes** and addresses are five bytes.

The locked RadioTransport v3 fragment overhead is therefore:

```text
15-byte fixed v3 prefix
+ 5-byte source RadioAddress
= 20 bytes
```

leaving **12 bytes** of logical payload per physical fragment. Because v3 fragment count is one byte, the exact provider-compatible logical maximum is therefore:

```text
12 * 255 = 3060 bytes
```

`NRF24Radio::Capabilities()` advertises `MaximumPayloadBytes = 32` and `MaximumLogicalTransferBytes = 3060`. Larger logical transfers are rejected by Radio admission; the provider does not add another fragmentation layer.

## Addressing and provenance

nRF24 endpoints are five-byte opaque `RadioAddress` values. They are link-local identifiers only, not permanent device identity, authentication credentials, Mesh aliases or routing authority.

The RF24 receive FIFO does not expose the transmitter endpoint, so the physical `RadioPacketView::Source` is intentionally invalid. Ordinary v3 traffic carries its immutable Radio source address in the Radio-owned v3 header, allowing Radio reassembly to recover link-level source provenance without inventing a source inside the provider.

## Contention domain

A non-zero `NRF24RadioConfiguration::ContentionDomain` may be supplied explicitly. If omitted, the provider derives a deterministic non-zero contention-domain identity from the configured RF channel.

Providers sharing a physical medium must be composed into the same Radio contention domain so R3 serializes physical service correctly. Independent domains may progress concurrently.

## Ingress and resource bounds

The nRF24 hardware receive FIFO is treated as a finite provider-owned resource of three packets. `ServiceInbound(maximumPackets)` never drains without a bound: the service quantum is the lower of the caller-supplied limit and the three-packet hardware bound. If work remains, the provider re-signals the Radio runtime.

`ProviderResources()` reports:

- finite ingress capacity: 3 packets;
- finite ingress service quantum: 3 packets;
- provider TX queue depth: 0 (synchronous transmission);
- configured hardware retry count.

These provider-owned resources are distinct from Radio-owned Q1 record/byte capacity.

## Transmission evidence and cost

`RF24::write()` is synchronous. A successful unicast therefore returns `RadioDirectLinkEvidence::CompletedAndAcknowledged()`. Broadcast disables ACK at the link and returns `CompletedWithoutPeerAcknowledgement()`.

The conservative cost model includes configured retry attempts, RF payload/overhead allowance and retry-delay windows at the selected RF data rate. This permits R3 to use the provider for promotable service profiles without pretending that retries are free.

## Clock synchronization limitation

The RF24 API used by this provider exposes no provider-proximate receive timestamp. RX evidence is therefore published as `RadioTimestampQuality::Unbounded` and the provider does **not** advertise `ReceiveTimestamp` or `TransmitTimestamp` capability.

The 32-byte MTU can carry the exact compact Radio Clock response physically, but the provider is not certified for K1/K2 synchronization until a genuinely bounded capture source is implemented and characterized. Software must not manufacture a sub-millisecond claim from service-context arrival time.

## Example configuration

```cpp
#include <ESPressio_NRF24.hpp>

ESPressio::NRF24::NRF24RadioConfiguration configuration;
configuration.CePin = 4;
configuration.CsnPin = 5;
configuration.LocalAddress = ESPressio::Radio::RadioAddress::FromBytes(
    reinterpret_cast<const std::uint8_t*>("NODE1"), 5);

ESPressio::NRF24::NRF24Radio radio(configuration);
```

The application composes this provider into the appropriate `ESPressio-Radio` domain runtime. It does not construct a provider-specific Radio worker.

## Coordinated redesign branch

During the Primitive Platform redesign tranche, consume `ESPressio-Radio`, its System/Task/Timing/Units dependencies, and this repository from their coordinated `primitives_redesign` branches. No version/tag/main-branch release claim is implied by the tranche implementation.