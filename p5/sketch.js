// 전역 변수
let port;
let portConnected = false;
let latestData = "";

// 파싱된 데이터
let brightnessValue = "";
let modeValue = "";
let ledState = [0, 0, 0];  // [red, yellow, green]

// HTML 요소 참조
let connectButton;
let rSlider, ySlider, gSlider;
let lastSentTime = 0;
let sendInterval = 500; // 500ms마다 슬라이더 값 전송

function setup() {
  // (1) p5.js의 createCanvas()를 만들지 않거나, 만들어도 숨겨서 사용
  // createCanvas(1, 1);  // 또는 아예 생략 가능
  // noLoop();           // draw()를 사용하지 않으려면 noLoop() 호출

  // (2) p5.dom 함수를 사용해 HTML 요소 선택
  connectButton = select("#connectButton");
  connectButton.mousePressed(connectSerial);

  rSlider = select("#rSlider");
  ySlider = select("#ySlider");
  gSlider = select("#gSlider");

  // (3) draw() 대신 setInterval 등으로 주기적 작업 수행하거나,
  //     p5.js의 draw() 내에서 처리 가능
}

// draw()가 필요 없다면, 아래 함수 안의 내용은 제거 가능
function draw() {
  // 슬라이더 값 주기적으로 전송
  if (portConnected && millis() - lastSentTime > sendInterval) {
    sendSliderValues();
    lastSentTime = millis();
  }
}

/**
 * 사용자가 시리얼 포트를 선택하고, 연결을 시도하는 함수
 */
async function connectSerial() {
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 9600 });
    portConnected = true;
    connectButton.html("Serial Connected");
    readLoop();
  } catch (error) {
    console.log("Serial connection error: " + error);
  }
}

/**
 * 시리얼 데이터를 읽는 루프
 */
async function readLoop() {
  const decoder = new TextDecoder();
  while (port.readable) {
    const reader = port.readable.getReader();
    try {
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        if (value) {
          latestData += decoder.decode(value);
          // 줄바꿈(\n) 단위로 분할하여 처리
          if (latestData.indexOf("\n") !== -1) {
            let lines = latestData.split("\n");
            let completeLine = lines[0].trim();
            processSerialData(completeLine);
            latestData = lines.slice(1).join("\n");
          }
        }
      }
    } catch (error) {
      console.log("Read error: " + error);
    } finally {
      reader.releaseLock();
    }
  }
}

/**
 * 시리얼로부터 받은 데이터 파싱
 * 예: "B: 160 M: PCINT2 O: 1,0,1"
 */
function processSerialData(dataStr) {
  const pattern = /^B:\s*(\d+)\s*M:\s*(\S+)\s*O:\s*([\d,]+)/;
  const match = dataStr.match(pattern);

  if (match) {
    let newBrightness = match[1];
    let newMode = match[2];
    let ledStates = match[3];

    // Brightness
    if (!isNaN(newBrightness) && newBrightness !== "") {
      brightnessValue = newBrightness;
    }

    // Mode
    let validModes = ["PCINT1", "PCINT2", "PCINT3", "Default"];
    if (validModes.includes(newMode)) {
      switch (newMode) {
        case "PCINT1":
          modeValue = "Mode1";
          break;
        case "PCINT2":
          modeValue = "Mode2";
          break;
        case "PCINT3":
          modeValue = "Mode3";
          break;
        default:
          modeValue = "Default";
          break;
      }
    }

    // LED 상태
    let states = ledStates.split(",");
    if (states.length === 3) {
      ledState[0] = parseInt(states[0]); // Red
      ledState[1] = parseInt(states[1]); // Yellow
      ledState[2] = parseInt(states[2]); // Green
    }

    // UI 업데이트
    updateInfoDisplay();
    updateIndicators();
  }
}

/**
 * 슬라이더 값 전송
 */
async function sendSliderValues() {
  if (port && port.writable) {
    const encoder = new TextEncoder();
    let dataToSend =
      rSlider.value() + "," + ySlider.value() + "," + gSlider.value() + "\n";
    const writer = port.writable.getWriter();
    await writer.write(encoder.encode(dataToSend));
    writer.releaseLock();
  }
}

/**
 * Brightness/Mode 표시 업데이트
 */
function updateInfoDisplay() {
  const infoElement = document.getElementById("serialInfo");
  infoElement.textContent = `Brightness : ${brightnessValue} / Mode : ${modeValue}`;
}

/**
 * 신호등 인디케이터 업데이트
 * ledState = [R, Y, G], brightnessValue = 0~255
 */
function updateIndicators() {
  let bVal = parseInt(brightnessValue);
  if (isNaN(bVal)) bVal = 0;

  // DOM 요소 가져오기
  const redIndicator = document.getElementById("red-indicator");
  const yellowIndicator = document.getElementById("yellow-indicator");
  const greenIndicator = document.getElementById("green-indicator");

  // 빨강
  if (ledState[0] === 1) {
    redIndicator.style.backgroundColor = `rgb(${bVal}, 0, 0)`;
  } else {
    redIndicator.style.backgroundColor = `rgb(${bVal * 0.2}, 0, 0)`;
  }

  // 노랑 (빨강+초록)
  if (ledState[1] === 1) {
    yellowIndicator.style.backgroundColor = `rgb(${bVal}, ${bVal}, 0)`;
  } else {
    yellowIndicator.style.backgroundColor = `rgb(${bVal * 0.2}, ${bVal * 0.2}, 0)`;
  }

  // 초록
  if (ledState[2] === 1) {
    greenIndicator.style.backgroundColor = `rgb(0, ${bVal}, 0)`;
  } else {
    greenIndicator.style.backgroundColor = `rgb(0, ${bVal * 0.2}, 0)`;
  }
}
