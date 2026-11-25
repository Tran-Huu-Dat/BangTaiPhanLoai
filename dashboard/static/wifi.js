(() => {
  const $ = id => document.getElementById(id);
  const dialog = $("wifi-dialog"), list = $("wifi-list"), form = $("wifi-form");
  const text = {
    HOST_SCAN_FAILED: "Windows không quét được Wi‑Fi. Kiểm tra card Wi‑Fi và thử lại.",
    WIFI_PERMISSION_DENIED: "Windows chưa cho phép đọc Wi‑Fi. Kiểm tra quyền Vị trí trong Cài đặt Windows.",
    WINDOWS_WIFI_ONLY: "Xem mạng thật ở chế độ mô phỏng cần chạy dashboard trên Windows.",
    NO_WIFI_ADAPTER: "Không tìm thấy card Wi‑Fi trên máy tính chạy dashboard.",
    SIMULATED_DEVICE: "Đang mô phỏng. Chọn thiết bị Pico thật để cấu hình Wi‑Fi.",
    OFFLINE: "Thiết bị đang ngoại tuyến.",
    WIFI_BUSY: "Thiết bị đang xử lý yêu cầu Wi‑Fi khác.",
    BUSY: "Thiết bị đang bận, thử lại sau giây lát.",
    MQTT_SEND_FAILED: "Không gửi được yêu cầu tới broker.",
    SCAN_FAILED: "Thiết bị không quét được mạng.",
    BAD_CREDENTIALS: "Tên mạng hoặc mật khẩu không hợp lệ (mật khẩu 8–63 ký tự).",
    WIFI_FAILED: "Không vào được mạng này, kiểm tra mật khẩu. Thiết bị đã quay về mạng cũ.",
    BROKER_UNREACHABLE: "Vào được mạng nhưng không thấy broker MQTT. Máy tính phải cùng mạng này. Thiết bị đã quay về mạng cũ.",
    TIMEOUT: "Chưa nhận được phản hồi. Nếu thiết bị đã đổi mạng, đưa máy tính sang cùng mạng rồi quét lại."
  };
  let selected = null, busy = false, source = null;

  const say = message => { $("wifi-status").textContent = message; };
  const bars = rssi => rssi > -55 ? "▂▄▆█" : rssi > -67 ? "▂▄▆" : rssi > -78 ? "▂▄" : "▂";

  async function call(url, body) {
    const options = body === undefined ? {cache: "no-store"} :
      {method: "POST", headers: {"Content-Type": "application/json"}, body: JSON.stringify(body)};
    options.signal = AbortSignal.timeout(15000);
    const response = await fetch(url, options);
    const data = await response.json();
    if (!response.ok) throw new Error(typeof data.detail === "string" ? data.detail : "BAD_CREDENTIALS");
    return data;
  }

  // Waits for the device's answer to one request (scan or connect).
  async function waitFor(kind, requestId, seconds) {
    const end = Date.now() + seconds * 1000;
    while (Date.now() < end) {
      await new Promise(resolve => setTimeout(resolve, 700));
      let state;
      try { state = await call("/api/wifi"); } catch { continue; }  // Backend may blink during the switch.
      const result = state[kind];
      if (result && result.request_id === requestId) return result;
    }
    return {ok: false, reason: "TIMEOUT"};
  }

  function render(networks) {
    list.replaceChildren(...networks.map(net => {
      const item = document.createElement("li"), button = document.createElement("button");
      const name = document.createElement("span"), meta = document.createElement("small");
      name.textContent = net.ssid;
      meta.textContent = `${net.secure ? "🔒 " : ""}${bars(net.rssi)} ${net.rssi} dBm`;
      button.type = "button";
      button.disabled = source !== "device";
      button.setAttribute("aria-pressed", String(selected === net.ssid));
      button.append(name, meta);
      button.addEventListener("click", () => pick(net));
      item.append(button);
      return item;
    }));
  }

  function pick(net) {
    selected = net.ssid;
    for (const button of list.querySelectorAll("button")) {
      button.setAttribute("aria-pressed", String(button.firstChild.textContent === net.ssid));
    }
    form.hidden = false;
    $("wifi-password").value = "";
    $("wifi-password").hidden = $("wifi-show").parentElement.hidden = !net.secure;
    $("wifi-target").textContent = net.secure ? `Mật khẩu cho “${net.ssid}”` : `“${net.ssid}” là mạng mở.`;
    (net.secure ? $("wifi-password") : $("wifi-connect")).focus();
  }

  function setBusy(value) {
    busy = value;
    $("wifi-scan").disabled = value;
    for (const control of [$("wifi-connect"), ...list.querySelectorAll("button")]) {
      control.disabled = value || source !== "device";
    }
  }

  async function scan() {
    if (busy) return;
    selected = null;
    source = null;
    list.replaceChildren();
    form.hidden = true;
    $("wifi-password").value = "";
    $("wifi-password").type = "password";
    $("wifi-show").checked = false;
    $("wifi-source").textContent = "Đang xác định nguồn quét Wi‑Fi…";
    setBusy(true);
    say("Đang quét mạng…");
    try {
      const state = await call("/api/wifi");
      source = state.source;
      $("wifi-title").textContent = source === "windows" ? "Wi‑Fi của máy tính" : "Wi‑Fi của thiết bị";
      $("wifi-source").textContent = source === "windows"
        ? "Mạng thật từ Windows trên máy chạy dashboard (tối đa 16 mạng). Thiết bị đang mô phỏng nên chỉ xem danh sách; chưa thể đổi Wi‑Fi cho Pico. Cường độ là giá trị ước tính."
        : `Quét từ ${state.device_id}. Chọn mạng 2,4 GHz mà máy tính này cũng đang dùng. Nếu mạng mới lỗi, thiết bị tự quay về mạng cũ.`;
      const {request_id} = await call("/api/wifi/scan", {});
      const result = await waitFor("scan", request_id, source === "windows" ? 45 : 20);
      if (result.ok) {
        render(result.networks);
        say(result.networks.length ? `Tìm thấy ${result.networks.length} mạng.` : "Không tìm thấy mạng nào.");
      } else say(text[result.reason] || result.reason);
    } catch (error) {
      say(text[error.message] || "Không gọi được máy chủ dashboard.");
    }
    setBusy(false);
  }

  form.addEventListener("submit", async event => {
    event.preventDefault();
    if (busy || !selected || source !== "device") return;
    setBusy(true);
    say("Đang chuyển mạng. Thiết bị sẽ ngoại tuyến vài giây…");
    try {
      const {request_id} = await call("/api/wifi/connect", {ssid: selected, password: $("wifi-password").value});
      const result = await waitFor("connect", request_id, 45);
      say(result.ok ? `Đã kết nối “${result.ssid || selected}” và lưu vào thiết bị.` : text[result.reason] || result.reason);
      if (result.ok) {
        form.hidden = true;
        $("wifi-password").value = "";
      }
    } catch (error) {
      say(text[error.message] || "Không gọi được máy chủ dashboard.");
    }
    setBusy(false);
  });

  $("wifi-show").addEventListener("change", event => {
    $("wifi-password").type = event.target.checked ? "text" : "password";
  });
  $("wifi-scan").addEventListener("click", scan);
  $("wifi-open").addEventListener("click", () => {
    dialog.showModal();
    scan();
  });
  dialog.addEventListener("close", () => {
    $("wifi-password").value = "";  // Never leave the password sitting in the page.
    $("wifi-password").type = "password";
    $("wifi-show").checked = false;
  });
})();
