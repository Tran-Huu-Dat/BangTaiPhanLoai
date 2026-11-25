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
    BUILD_FAILED: "Không build được file BangTaiPhanLoai.uf2. Kiểm tra thư mục build.",
    HOST_WIFI_NOT_CONNECTED: "Máy tính chưa kết nối Wi‑Fi để dùng chung.",
    TIMEOUT: "Chưa nhận được phản hồi. Nếu thiết bị đã đổi mạng, đưa máy tính sang cùng mạng rồi quét lại."
  };
  let selected = null, busy = false, source = null;
  const canConnect = () => source === "device" || source === "windows_provision";

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
      button.disabled = !canConnect();
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
    $("wifi-host-connect").disabled = value || !canConnect();
    for (const control of [$("wifi-connect"), ...list.querySelectorAll("button")]) {
      control.disabled = value || !canConnect();
    }
  }

  function showConnectResult(result, ssidLabel) {
    if (result.ok && typeof result.reason === "string" && result.reason.startsWith("PROVISIONED_UF2:")) {
      const ip = result.reason.slice("PROVISIONED_UF2:".length);
      say(`Đã lưu Wi‑Fi “${result.ssid || ssidLabel}” (broker ${ip}:1883) vào build/BangTaiPhanLoai.uf2. Chép file .uf2 vào Pico để kết nối chung Wi‑Fi.`);
    } else {
      say(result.ok ? `Đã kết nối “${result.ssid || ssidLabel}” và lưu vào thiết bị.` : text[result.reason] || result.reason);
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
      const hostBtn = $("wifi-host-connect");
      if (state.host_ssid && canConnect()) {
        hostBtn.hidden = false;
        hostBtn.textContent = `Dùng chung Wi‑Fi “${state.host_ssid}”`;
      } else {
        hostBtn.hidden = true;
      }
      $("wifi-title").textContent = source === "windows"
        ? "Wi‑Fi của máy tính"
        : source === "windows_provision"
          ? "Cấu hình Wi‑Fi chung cho Pico"
          : "Wi‑Fi của thiết bị";
      $("wifi-source").textContent = source === "windows"
        ? "Mạng thật từ Windows trên máy chạy dashboard (tối đa 16 mạng). Thiết bị đang mô phỏng nên chỉ xem danh sách; chưa thể đổi Wi‑Fi cho Pico. Cường độ là giá trị ước tính."
        : source === "windows_provision"
          ? "Thiết bị chưa nối Wi‑Fi. Chọn mạng Wi‑Fi 2,4 GHz mà máy tính đang dùng và nhập mật khẩu để tạo file BangTaiPhanLoai.uf2 sẵn sàng nạp vào Pico."
          : `Quét từ ${state.device_id}. Chọn mạng 2,4 GHz mà máy tính này cũng đang dùng. Nếu mạng mới lỗi, thiết bị tự quay về mạng cũ.`;
      const {request_id} = await call("/api/wifi/scan", {});
      const result = await waitFor("scan", request_id, source === "device" ? 20 : 45);
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
    if (busy || !selected || !canConnect()) return;
    setBusy(true);
    say(source === "windows_provision"
      ? "Đang lưu cấu hình Wi‑Fi và tạo file BangTaiPhanLoai.uf2…"
      : "Đang chuyển mạng. Thiết bị sẽ ngoại tuyến vài giây…");
    try {
      const {request_id} = await call("/api/wifi/connect", {ssid: selected, password: $("wifi-password").value});
      const result = await waitFor("connect", request_id, 45);
      showConnectResult(result, selected);
      if (result.ok) {
        form.hidden = true;
        $("wifi-password").value = "";
      }
    } catch (error) {
      say(text[error.message] || "Không gọi được máy chủ dashboard.");
    }
    setBusy(false);
  });

  $("wifi-host-connect").addEventListener("click", async () => {
    if (busy || !canConnect()) return;
    setBusy(true);
    say("Đang lấy Wi‑Fi máy tính đang dùng và tạo file BangTaiPhanLoai.uf2…");
    try {
      const {request_id, ssid} = await call("/api/wifi/connect-host", {});
      const result = await waitFor("connect", request_id, 45);
      showConnectResult(result, ssid || "máy tính");
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
