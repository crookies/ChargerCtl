const username = "admin";  // Change to match ESP32 code
const password = "1234";   // Change to match ESP32 code

// Encode credentials in Base64
const basicAuth = "Basic " + btoa(username + ":" + password);

document.addEventListener("DOMContentLoaded", function() {
    function fetchData() {
        fetch("/status") //No manual authorization header
        .then(response => {
            if (response.status === 401) {
                alert("Unauthorized! Please check your credentials.");
                return;
            }
            return response.json();
        })
        .then(data => updatePage(data))
        .catch(error => console.error("Error fetching data:", error));
    }

    // Fetch data initially
    fetchData();

    // Set interval to fetch data every 5 seconds
    setInterval(fetchData, 5000);
});

function updatePage(data) {
    // MultiPlus Status
    document.getElementById("multiplus-status").textContent = data.multiplus_status;
    if (data.multiplus_status === "Charging") {
        document.getElementById("multiplus-dc").textContent = data.multiplus_dc_out + " V";
        document.getElementById("multiplus-current").textContent = data.multiplus_current + " A";
        document.getElementById("multiplus-extra").classList.remove("hidden");
    }

    // Charging Mode
    document.getElementById("charging-mode").textContent = data.charging_mode;

    // Modbus Status
    let modbusSection = document.getElementById("modbus-section");
    document.getElementById("modbus-status").textContent = data.modbus_status;
    document.getElementById("modbus-message-counter").textContent = data.modbus_message_counter;

    // If Modbus status is ERROR, make the section red
    if (data.modbus_status === "ERROR") {
        modbusSection.classList.add("error");
    } else {
        modbusSection.classList.remove("error");
    }
}

function goToConfig() {
    window.location.href = "config.html";
}
