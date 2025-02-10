import { Component, OnInit } from '@angular/core';
import { RouterOutlet } from '@angular/router';
import { SysDataService } from './api/sys-data.service';
import { PrimengModule } from './primeng.module';
import { SysData } from './classes/sys-data.class';
import { FormsModule } from '@angular/forms';

@Component({
  selector: 'app-root',
  imports: [RouterOutlet, PrimengModule, FormsModule],
  templateUrl: './app.component.html',
  styleUrl: './app.component.scss',
})
export class AppComponent implements OnInit {
  sysData: SysData = new SysData();
  delay = 3;  

  constructor(private dataService: SysDataService) {}

  ngOnInit(): void {
    this.getSysData();
    setInterval(() => {
      this.getSysData();
    }, this.delay * 1000);
  }

  setDelay(delay: number): void {
    this.delay = delay;
  }

  getSysData(): any {
    // Fetch system data from the API periodically
    this.dataService.getSysData().subscribe((data: SysData) => {
      this.sysData = data;
      this.sysData.free_ram.toFixed(2);
      console.log(this.sysData);
    });
  }
}
