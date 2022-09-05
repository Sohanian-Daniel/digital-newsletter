# digital-newsletter

Made for a Communication Protocols course in Spring 2022
Clients can subscribe to topics and receive messages only relating to what they want,
has a store and forward feature that sends clients all messages that they missed while offline!

Notes:
- The server receives messages on topics from UDP clients (not included here) and forwards them to all TCP clients.

Client Usage:
- ./subscriber [client ID] [server IP] [server PORT]
- subscribe [topic] [0/1 for store and forward]
- unsubscribe [topic]

TODO:
- Update the Application-level protocol that ensures data framing over TCP to a more efficient protocol.
