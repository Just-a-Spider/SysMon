import { TestBed } from '@angular/core/testing';

import { SysDataService } from './sys-data.service';

describe('SysDataService', () => {
  let service: SysDataService;

  beforeEach(() => {
    TestBed.configureTestingModule({});
    service = TestBed.inject(SysDataService);
  });

  it('should be created', () => {
    expect(service).toBeTruthy();
  });
});
