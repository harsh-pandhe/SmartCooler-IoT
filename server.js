const express = require('express');
const admin = require('firebase-admin');
const mqtt = require('mqtt');
const cors = require('cors');
const path = require('path');

const app = express();
app.use(express.json());
app.use(cors());

// --- 1. INITIALIZE FIREBASE ---
// Ensure your serviceAccountKey.json is in the same directory!
const serviceAccount = require("./serviceAccountKey.json");
admin.initializeApp({
    credential: admin.credential.cert(serviceAccount),
    databaseURL: "https://smartwatercooler-ed8ae-default-rtdb.firebaseio.com"
});
const db = admin.database();

// --- 2. INITIALIZE MQTT ---
const mqttClient = mqtt.connect('mqtt://broker.hivemq.com');
const MQTT_TOPIC_CMD = "hydrochill/command";

mqttClient.on('connect', () => {
    console.log('Connected to HiveMQ MQTT Broker');
});

// --- 3. SERVE DASHBOARD ---
app.get('/', (req, res) => {
    res.sendFile(path.join(__dirname, 'index.html'));
});

// --- 4. API ENDPOINTS ---

// ESP32 sends data here via HTTP POST
app.post('/api/update', async (req, res) => {
    try {
        const data = req.body;
        // Sync to Firebase Realtime Database
        await db.ref('cooler_status').update({
            ...data,
            serverTimestamp: Date.now()
        });
        console.log("ESP32 Sync:", data.waterTemp, "°C |", data.roomTemp, "°C");
        res.status(200).send({ status: "success" });
    } catch (err) {
        console.error("Firebase Sync Error:", err);
        res.status(500).send({ error: "Internal Server Error" });
    }
});

// Dashboard sends commands here via HTTP POST
app.post('/api/command', (req, res) => {
    const { type, value } = req.body;
    let cmdStr = "";

    if (type === 'SET_TEMP') cmdStr = `SET:${value}`;
    else if (type === 'TOGGLE_MODE') cmdStr = "MODE:TOGGLE";

    if (cmdStr) {
        mqttClient.publish(MQTT_TOPIC_CMD, cmdStr);
        console.log("MQTT Command Dispatched:", cmdStr);
        res.json({ success: true });
    } else {
        res.status(400).json({ error: "Invalid Command" });
    }
});

const PORT = 3000;
app.listen(PORT, () => {
    console.log(`=========================================`);
    console.log(`HYDROCHILL GATEWAY ACTIVE ON PORT ${PORT}`);
    console.log(`URL: http://localhost:${PORT}`);
    console.log(`=========================================`);
});