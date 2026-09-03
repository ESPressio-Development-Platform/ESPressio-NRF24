# ESPressio-NRF24

`ESPressio-NRF24` provides the nRF24L01/nRF24L01+ concrete `IRadio` implementation for `ESPressio-Radio`.

The provider remains strictly a physical/link concrete. It transports opaque bytes, owns no ESPressio Device or Mesh identity, performs no Mesh routing, and does not interpret Command, Event, State or another conceptual primitive family.

## Physical and logical transfer bounds

The nRF24 hardware physical payload ceiling is **32 bytes**. The provider advertises that physical limit through `RadioCapabilities::MaximumPayloadBytes` and advertises a finite **4096-byte** Radio logical-transfer ceiling through `MaximumLogicalTransferBytes`.

Complete logical transfers larger than 32 bytes are fragmented/reassembled by `ESPressio-Radio::RadioTransport`; fragmentation is not implemented independently inside this concrete provider.

## Receive-source behaviour

The nRF24 receive FIFO does not report the transmitter endpoint. Accordingly, physical `RadioPacketView::Source` is intentionally left invalid.

For ordinary logical Radio transfers, `RadioTransport` includes the sending `RadioAddress` in its own bounded fragment framing, so a complete reassembled transfer still has Radio-level source provenance without inventing a DeviceIdentifier or leaking Mesh semantics into this provider.

The precision T1/T2/T3/T4 clock exchange remains a separate direct-link mechanism and bypasses ordinary RadioTransport fragmentation. Its request framing already carries the required return endpoint, preserving the 32-byte atomic physical exchange.

## Addressing

nRF24 addresses are five-byte opaque `RadioAddress` values. They are technology-specific link endpoints only; they are not permanent device identities, authentication credentials, Mesh node aliases or routing authority.

## Worker ownership

Inbound hardware servicing is driven by `RadioWorker`. Applications do not need a second polling layer around this provider. `DrainInbound()` reads available bounded RF24 payloads and passes borrowed physical packet views to the worker-owned receiver path.

During the Mesh structural-real\-ignment tranche, `ESPressio-Radio` is pinned to `structural_realignment_propagation_ESPressio-Mesh` so the concrete is compiled against the matching direct-link Radio contract.
