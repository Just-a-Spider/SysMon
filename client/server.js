const express = require("express");
const cors = require("cors");
const path = require("path");
const { exec } = require("child_process");
const SystemHealthMonitor = require("system-health-monitor");

const app = express();
const PORT = process.env.PORT || 3000;

const monitorConfig = {
  checkIntervalMsec: 1000,
  mem: {
    thresholdType: "none",
  },
  cpu: {
    calculationAlgo: "last_value",
    thresholdType: "none",
  },
};

const monitor = new SystemHealthMonitor(monitorConfig);

// Serve Angular app (build)
app.use(express.static(path.join(__dirname, "dist/client/browser")));

// CORS middleware for API routes
app.use(cors("*"));

// Get system data from sensors command and return it
app.get("/api/data", async (req, res) => {
  monitor
    .start()
    .then(async () => {
      let freeMemory = monitor.getMemFree();
      // Convert free memory to GB
      freeMemory = (freeMemory / 1024).toFixed(2);
      // Apply correction factor based on the range of freeMemory
      if (freeMemory >= 11.0 && freeMemory < 13.0) {
        freeMemory -= 0.7;
      } else if (freeMemory >= 9.0 && freeMemory < 11.0) {
        freeMemory -= 0.9;
      } else if (freeMemory >= 6.0 && freeMemory < 9.0) {
        freeMemory -= 1.1;
      } else if (freeMemory >= 4.0 && freeMemory < 6.0) {
        freeMemory -= 1.23;
      }
      // Fix to 2 decimal places and as a number in the JSON
      const fixedFreeMemory = parseFloat(freeMemory.toFixed(2));

      const cpuUsage = monitor.getCpuUsage();
      const data = await parseSystemData();
      data.free_ram = fixedFreeMemory;
      data.cpu_usage = cpuUsage;
      res.json(data);

      monitor.stop();
    })
    .catch((error) => {
      console.error(error);
      monitor.stop();
    });
});

// Serve Angular app for all other routes
app.get("*", (req, res) => {
  res.sendFile(path.join(__dirname, "dist/client/browser/index.html"));
});

app.listen(PORT, () => {
  console.log(`Server is running on port ${PORT}`);
});

// Function to parse system data
async function parseSystemData() {
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
          if (line.includes("Tctl")) {
            return {
              name: "Tctl",
              value: parseFloat(line.split(":")[1].trim()),
            };
          } else if (line.includes("cpu_fan")) {
            return {
              name: "CPU Fan",
              value: parseInt(line.split(":")[1].trim(), 10),
            };
          } else if (line.includes("gpu_fan")) {
            return {
              name: "GPU Fan",
              value: parseInt(line.split(":")[1].trim(), 10),
            };
          } else if (line.includes("edge")) {
            return {
              name: "Edge",
              value: parseFloat(line.split(":")[1].trim()),
            };
          }
        })
        .filter((line) => line !== undefined); // Filter out undefined values

      // Convert array of objects to a single JSON object
      const jsonData = parsedData.reduce((acc, curr) => {
        acc[curr.name.toLowerCase().replace(" ", "_")] = curr.value;
        return acc;
      }, {});

      resolve(jsonData);
    });
  });
}
