import { HttpClient } from '@angular/common/http';
import { Injectable } from '@angular/core';
import { Observable } from 'rxjs';
import { SysData } from '../classes/sys-data.class';

@Injectable({
  providedIn: 'root',
})
export class SysDataService {
  private apiUrl = 'http://localhost:3000/api/data';

  constructor(private http: HttpClient) {}

  getSysData(): Observable<SysData> {
    return this.http.get<SysData>(this.apiUrl);
  }
}
