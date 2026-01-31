const statusEl = document.getElementById("status");
const warningEl = document.getElementById("webapp-warning");
const screenshotBtn = document.getElementById("screenshot-btn");

const tg = window.Telegram?.WebApp;

const setStatus = (text) => {
  statusEl.textContent = text;
};

if (!tg) {
  warningEl.hidden = false;
  setStatus("Откройте Web App через Telegram.");
} else {
  tg.ready();
  tg.expand();
}

screenshotBtn.addEventListener("click", () => {
  if (!tg) {
    setStatus("Команда недоступна вне Telegram.");
    return;
  }
  setStatus("Команда отправлена, ожидайте скриншот в чате.");
  tg.sendData("screenshot");
});
