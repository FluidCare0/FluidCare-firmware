# 📘 Firmware Guideline: Node Reset Flow (203 / 204)

## 🎯 Objective

Introduce a **safe deregistration mechanism** for nodes using a **request–acknowledgement model**.

* **203 → Reset Request**
* **204 → Reset Acknowledgement**

This ensures synchronization between **Node, Master, and Backend**.

---

# 🔢 Status Code Summary

| Code    | Direction     | Description           |
| ------- | ------------- | --------------------- |
| 200     | Node → Master | Sensor data           |
| 201     | Node → Master | Request Node-ID       |
| 202     | Master → Node | Node-ID assigned      |
| **203** | Node → Master | Request Node reset    |
| **204** | Master → Node | Reset confirmed (ACK) |

---

# ⚙️ Existing Firmware Flow

### 1. Registration

* Node sends **201**
* Master forwards to backend
* Backend responds with **202**
* Node stores Node-ID (persistent)

---

### 2. Data Transmission

* Node periodically sends **200**
* Master forwards to backend (MQTT)

---

# 🔴 New Reset Flow (Firmware Perspective)

### Step-by-step behavior:

### 1. Trigger

* Reset is initiated via **physical button press**

---

### 2. Node Action

* Node sends **203 (Reset Request)** to Master
* Node **must retain Node-ID** at this stage

---

### 3. Master Action

* Receives **203**
* Extracts Node MAC
* Forwards request to backend via MQTT

---

### 4. Backend Action (Overview)

* Identifies node using MAC
* Deletes Node-ID mapping
* Marks node as **unregistered**
* Sends **204 (ACK)**

---

### 5. Master Action

* Receives **204**
* Routes ACK to correct node via ESP-NOW

---

### 6. Node Final Action

* On receiving **204**:

  * Deletes Node-ID from persistent storage
  * Resets internal state
  * Returns to **unregistered state**

---

# ⚠️ Mandatory Firmware Rules

## 1. ACK-Based Deletion (Critical)

* Node **must NOT delete Node-ID on 203**
* Deletion is allowed **only after receiving 204**

---

## 2. MAC-Based Identification

* All communication must rely on **Node MAC**
* Do not depend on Node-ID for routing

---

## 3. Retry Mechanism

* If **204 is not received**:

  * Node should retry sending **203** periodically
* Prevent deadlock state

---

## 4. State Transition

| State           | Description                |
| --------------- | -------------------------- |
| Registered      | Node has valid Node-ID     |
| Reset Requested | 203 sent, waiting for ACK  |
| Unregistered    | After 204, Node-ID cleared |

---

## 5. User Interaction Safety

* Reset should ideally be triggered via:

  * **Long press (2–3 seconds)**
* Prevent accidental resets

---

## 6. Master Role (Firmware Constraint)

* Master acts as:

  * **Transport bridge only**
* Must NOT:

  * Store business logic
  * Modify reset decisions

---

# 🧠 Backend Overview (For Firmware Awareness)

Firmware team only needs to know:

* Backend listens for **203**
* Uses **MAC address** to:

  * Identify node
  * Delete Node-ID mapping
* Sends **204** after successful deletion

👉 Backend is the **source of truth**

---

# 🔄 System Flow Diagram (Mermaid)

```mermaid
sequenceDiagram
    participant Node
    participant Master
    participant Backend

    %% Registration Flow
    Node->>Master: 201 (Request Node-ID)
    Master->>Backend: Forward request
    Backend-->>Master: 202 (Node-ID)
    Master-->>Node: 202 (Assign Node-ID)

    %% Normal Data Flow
    Node->>Master: 200 (Sensor Data)
    Master->>Backend: Publish data

    %% Reset Flow
    Node->>Master: 203 (Reset Request)
    Master->>Backend: Forward reset request
    Backend->>Backend: Delete Node-ID mapping
    Backend-->>Master: 204 (Reset ACK)
    Master-->>Node: 204 (ACK)

    Node->>Node: Delete Node-ID (after ACK)
    Node->>Master: 201 (Re-register)
```

---

# 🚀 Final Summary

* **203** → Initiates reset (Node → Backend)
* **204** → Confirms reset (Backend → Node)
* Node deletes identity **only after 204**
* System remains consistent across all layers
