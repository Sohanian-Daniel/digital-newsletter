# digital-newsletter

Made for a Communication Protocols course in Spring 2022

Clients can subscribe to topics and receive messages only relating to what they want.

The Server has a store and forward feature that sends clients all messages that they missed while offline!

Notes:
- The server receives messages about topics from UDP clients (not included here) and forwards them to all TCP clients.

Client Usage:
- ./subscriber [CLIENT ID] [SERVER IP] [SERVER PORT]
  Commands:
  - subscribe [TOPIC] [0/1 for store and forward]
  - unsubscribe [TOPIC]
  - exit

Server Usage:
- ./server [SERVER PORT]

UDP Client:
- Check the README.md for the udp client.

TODO:
- Update the Application-level protocol that ensures data framing over TCP to a more efficient protocol.
