document.addEventListener("DOMContentLoaded", function() {
    fetch("/config")
        .then(response => response.json())
        .then(data => loadConfig(data))
        .catch(error => showNotification("Error loading config", "error"));
});

function loadConfig(data) {
    document.getElementById("chrg-ip").value = data.chrg_ip || "";
    document.getElementById("chrg-mask").value = data.chrg_mask || "";
    document.getElementById("chrg-gateway").value = data.chrg_gateway || "";
    document.getElementById("chrg-password").value = data.chrg_password || "";
    document.getElementById("cerbo-ip").value = data.cerbo_ip || "";
    document.getElementById("cerbo-unitid").value = data.cerbo_unitid || "";
    document.getElementById("chrg-wifi-ssid").value = data.chrg_wifi_ssid || "";
    document.getElementById("chrg-wifi-pass").value = data.chrg_wifi_pass || "";
    document.getElementById("wifi-enabled").checked = data.wifi_enabled;
    document.getElementById("ethernet-enabled").checked = data.ethernet_enabled;
    document.getElementById("wifi-ip").value = data.wifi_ip || "";
    document.getElementById("wifi-mask").value = data.wifi_mask || "";
    document.getElementById("wifi-gateway").value = data.wifi_gateway || "";

    toggleConfigSections();
}

function saveConfig() {
    let configData = {
        chrg_ip: document.getElementById("chrg-ip").value,
        chrg_mask: document.getElementById("chrg-mask").value,
        chrg_gateway: document.getElementById("chrg-gateway").value,
        chrg_password: document.getElementById("chrg-password").value,
        cerbo_ip: document.getElementById("cerbo-ip").value,
        cerbo_unitid: document.getElementById("cerbo-unitid").value,
        chrg_wifi_ssid: document.getElementById("chrg-wifi-ssid").value,
        chrg_wifi_pass: document.getElementById("chrg-wifi-pass").value,
        wifi_enabled: document.getElementById("wifi-enabled").checked,
        ethernet_enabled: document.getElementById("ethernet-enabled").checked,
        wifi_ip: document.getElementById("wifi-ip").value,
        wifi_mask: document.getElementById("wifi-mask").value,
        wifi_gateway: document.getElementById("wifi-gateway").value
    };

    fetch("/save_config", {
        method: "POST",
        headers: {
            "Content-Type": "application/json"
        },
        body: JSON.stringify(configData)
    })
    .then(response => response.json())
    .then(data => showNotification("Configuration Saved!", "success"))
    .catch(error => showNotification("Error saving configuration", "error"));
}

function toggleConfigSections() {
    let wifiEnabled = document.getElementById("wifi-enabled").checked;
    let ethernetEnabled = document.getElementById("ethernet-enabled").checked;

    document.getElementById("wifi-config-section").style.display = wifiEnabled ? "block" : "none";
    document.getElementById("ethernet-config-section").style.display = ethernetEnabled ? "block" : "none";
}

document.getElementById("wifi-enabled").addEventListener("change", toggleConfigSections);
document.getElementById("ethernet-enabled").addEventListener("change", toggleConfigSections);

function goBack() {
    window.location.href = "index.html";
}

function showNotification(message, type) {
    let notification = document.getElementById("notification");
    notification.textContent = message;
    notification.className = `notification ${type}`;
    notification.classList.remove("hidden");

    // Fade out after 3 seconds
    setTimeout(() => {
        notification.classList.add("hidden");
    }, 3000);
}
