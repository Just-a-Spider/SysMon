const express = require("express");
const cors = require("cors");
const path = require("path");
const si = require("systeminformation");
const { exec } = require("child_process");

const app = express();
const PORT = process.env.PORT || 3000;

// Serve Angular app (build)
app.use(express.static(path.join(__dirname, "dist/client/browser")));

// CORS middleware for API routes
app.use(cors("*"));

console.log("Server starting...");
parseSystemData();

// Get system data from sensors command and return it
app.get("/api/data", async (req, res) => {
  const data = await parseSystemData();
  res.json(data);
});

// Serve Angular app for all other routes
app.get("*", (req, res) => {
  res.sendFile(path.join(__dirname, "dist/client/browser/index.html"));
});

app.listen(PORT, () => {
  console.log(`Server is running on port ${PORT}`);
});

async function getFansSpeed() {
  return new Promise((resolve, reject) => {
    exec("sensors", (error, stdout, stderr) => {
      if (error) {
        console.error(`exec error: ${error}`);
        return reject(error);
      }
      const data = stdout.split("\n");
      // Filter out empty strings and JSONify the data of: Tctl, cpu_fan, gpu_fan, edge
      const parsedData = data
        .filter((line) => line !== "")
        .map((line) => {
          if (line.includes("cpu_fan")) {
            return {
              name: "cpu_fan",
              value: parseInt(line.split(":")[1].trim(), 10),
            };
          } else if (line.includes("gpu_fan")) {
            return {
              name: "gpu_fan",
              value: parseInt(line.split(":")[1].trim(), 10),
            };
          } else if (line.includes("edge")) {
            return {
              name: "gpu_temp",
              value: parseFloat(line.split(":")[1].trim()),
            };
          }
        })
        .filter((line) => line !== undefined); // Filter out undefined values

      // Convert array of objects to a single JSON object
      const jsonData = parsedData.reduce((acc, curr) => {
        acc[curr.name] = curr.value;
        return acc;
      }, {});

      resolve(jsonData);
    });
  });
}
// Function to parse system data
async function parseSystemData() {
  // CPU data
  const cpuTemp = await si.cpuTemperature().then((data) => data.main);
  const cpuUsage = await si
    .currentLoad()
    .then((data) => data.currentLoad.toFixed(2));

  // GPU data
  // const graphicsData = await si.graphics();
  // const gpuTemp = graphicsData.controllers[0].temperatureGpu;

  // RAM data
  const freeRam = await si
    .mem()
    .then((data) => parseFloat((data.available / 1000000000).toFixed(2)));

  // Fan data
  const fansData = await getFansSpeed();

  return {
    cpu_fan: fansData.cpu_fan,
    gpu_fan: fansData.gpu_fan,
    cpu_temp: cpuTemp,
    gpu_temp: fansData.gpu_temp,
    free_ram: freeRam,
    cpu_usage: cpuUsage,
  };
}
