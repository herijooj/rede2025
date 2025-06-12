### Overview
The goal is to create a **treasure hunt game** using a **client-server model**. The communication between the client and server will use **raw sockets**, meaning you'll work at a low level (Layer 2) without relying on higher-level protocols like TCP/IP. Here's the big picture:

- **Server**: Manages the game map and treasures. It displays:
  - Positions of the treasures.
  - A log of user movements.
  - The current position of the player.
- **Client**: Handles user interaction. It:
  - Displays an 8x8 grid.
  - Accepts user input to move the player.
  - Shows treasures when discovered.
- **Execution**:
  - The server and client must run on **separate machines**.
  - Machines need **root access** (e.g., boot from a USB drive).
  - Connect the machines directly with a **network cable** (no switches, as the custom frames won't pass through them).
- **Program Structure**: You can either:
  - Write one program with parameters to act as client or server, or
  - Create two separate programs, one for each role.

---

### Game Mechanics
Here's how the game works:

- **Map**: An **8x8 grid**, with coordinates starting at (0,0) in the bottom-left corner.
- **Treasures**:
  - The server randomly places **8 treasures** on the grid at startup.
  - Treasure locations are **not sent to the client** initially—they're only revealed when the player reaches them.
  - Treasure positions are shown on the server's console, along with a log of movements and treasure discoveries.
- **Player Movement**:
  - The user controls a player (agent) via the client.
  - Allowed moves: **right, up, down, left** (one position at a time).
  - **Invalid moves** (e.g., moving outside the 8x8 grid) should be ignored, with an optional error message.
- **Treasure Discovery**:
  - When the player lands on a treasure's position for the first time, the server sends the treasure file to the client.
  - Treasures are files of type:
    - Images (`.jpg`)
    - Videos (`.mp4`)
    - Text (`.txt`)
  - Files are stored in a server directory called **`objetos`**, named `1.xxx` to `8.xxx` (e.g., `1.jpg`, `2.mp4`, `3.txt`).
- **Client Display**:
  - Shows the 8x8 grid.
  - Marks **visited positions** and, differently, positions with **treasures** once discovered.
  - Displays the treasure (e.g., opens the image, plays the video, or shows the text) based on its type.

---

### Communication Protocol
The game uses a **custom protocol** inspired by Kermit. Here's the frame structure and message types:

#### Frame Format
Each message (frame) has these fields, sent in this order:

| Field          | Size     | Description                                                                 |
|----------------|----------|-----------------------------------------------------------------------------|
| Start Marker   | 8 bits   | Fixed value: `0111 1110` (marks the start of a frame)                      |
| Size           | 7 bits   | Number of bytes in the Data field (0 to 127)                               |
| Sequence       | 5 bits   | Sequence number for ordering messages (0 to 31)                            |
| Type           | 4 bits   | Message type (see below)                                                   |
| Checksum       | 8 bits   | Calculated over Size, Sequence, Type, and Data fields                      |
| Data           | 0-127 bytes | Payload (e.g., file data, file name, or empty, depending on the message) |

- **Checksum**: Ensures data integrity. Compute it over the Size, Sequence, Type, and Data fields.
- **Data Constraints**: Use **unsigned char (uchar)** for fields. Avoid floats in the protocol.

#### Message Types
The Type field (4 bits) defines the message purpose:

| Type | Meaning                  | Description                                      |
|------|--------------------------|--------------------------------------------------|
| 0    | ACK                      | Acknowledgment of a received message            |
| 1    | NACK                     | Negative acknowledgment (request retransmission)|
| 2    | OK + ACK                 | Confirms success plus acknowledgment            |
| 3    | Free                     | Unused (available for custom use)               |
| 4    | Size                     | Sends the size of a file to be transferred      |
| 5    | Data                     | Carries file data chunks                        |
| 6    | Text + ACK + Name        | Text file transfer with name and acknowledgment |
| 7    | Video + ACK + Name       | Video file transfer with name and acknowledgment|
| 8    | Image + ACK + Name       | Image file transfer with name and acknowledgment|
| 9    | End of File              | Marks the end of a file transfer                |
| 10   | Move Right               | Player moves right                              |
| 11   | Move Up                  | Player moves up                                 |
| 12   | Move Down                | Player moves down                               |
| 13   | Move Left                | Player moves left                               |
| 14   | Free                     | Unused (available for custom use)               |
| 15   | Error                    | Indicates an error (see error codes)            |

#### Error Codes
When Type = 15 (Error), the Data field contains an error code:

| Code | Meaning                  | Description                                    |
|------|--------------------------|------------------------------------------------|
| 0    | No permission to access  | Access denied (e.g., file permissions)        |
| 1    | Insufficient space       | Client lacks space to store the file          |

#### Flow Control
- **Default**: Use **stop-and-wait** protocol:
  - Sender waits for an ACK (or NACK) before sending the next message.
- **Optional (Extra Credit)**: Implement **sliding window** with a window size of 3 and **go-back N**:
  - Allows sending up to 3 messages before requiring an ACK.
  - If an error occurs, retransmit from the last unacknowledged message.
  - Caps at 100 points, even with extra credit.

#### Sequence Numbers
- Range: 0 to 31 (5 bits).
- Handle wrap-around (e.g., after 31 comes 0) using serial number arithmetic:
  - Compare sequence numbers to detect order or missing messages.
  - Example: If last received = 30 and new = 1, the difference suggests a wrap-around, not a huge gap.

---

### Implementation Requirements
Here’s what you need to do technically:

- **Languages**: Use **C**, **C++**, or **Go**.
- **Networking**: Use **raw sockets** for communication.
- **File Handling**:
  - **Server**: Reads treasure files from the `objetos` directory.
    - File names: `1.xxx` to `8.xxx`, max 63 bytes, ASCII chars 0x20 to 0x7E.
    - Use `stat` to get file size (`st.st_size`).
  - **Client**: Receives files and displays them based on type (e.g., open `.jpg`, play `.mp4`, show `.txt`).
    - Check available space with `statvfs` (`st.f_bsize * st.f_bavail`).
- **Error Handling**:
  - Manage **timeouts** and communication failures.
  - Track the last sent/received message for retransmission if needed.
  - Ensure the client has enough space before file transfers.
- **Interface**:
  - Client: Display the grid and handle moves (console or GUI, your choice).
  - Server: Show treasure positions, movement log, and player position.

---

### Development Tips
- **File Transfers**:
  - Send the file size first (Type 4).
  - Send data in chunks (Type 5, max 127 bytes each).
  - End with Type 9 (End of File).
  - Include the file name and type (Types 6, 7, or 8) for the client to handle display.
- **Movement**:
  - Client sends move requests (Types 10-13).
  - Server validates and responds with ACK (0), NACK (1), or treasure data if applicable.
- **Testing**:
  - Test with various file sizes and types.
  - Simulate network errors (e.g., dropped messages) to ensure robustness.

---

### Deliverables
- **Code**: Fully functional client and server.
- **Report**: Written in **SBC article format**, printed, and submitted via UFPR Virtual.
- **Presentation**:
  - Held in the DINF lab (schedule TBD).
  - All group members must attend and understand the project.
  - Professors will provide test files during the presentation.
  - No late submissions allowed.

---

### How to Approach This
1. **Start Simple**:
   - Set up raw socket communication between two machines.
   - Implement the basic frame structure and test sending/receiving simple messages (e.g., ACK).
2. **Build the Game**:
   - Create the 8x8 grid and movement logic.
   - Add treasure placement and console logging on the server.
3. **File Transfer**:
   - Implement file reading (server) and display (client).
   - Handle different file types and space checks.
4. **Protocol Details**:
   - Add sequence numbers, checksums, and stop-and-wait.
   - Optionally, tackle sliding window for extra points.
5. **Polish**:
   - Design a clear grid display.
   - Test edge cases (invalid moves, full disk, timeouts).

---

This specification combines networking, file handling, and game logic into a challenging but manageable project. Focus on getting the communication working first, then layer the game mechanics on top. Good luck with your implementation! Let me know if you need help with specific parts as you go along.