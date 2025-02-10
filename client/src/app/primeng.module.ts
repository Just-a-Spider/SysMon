import { NgModule } from '@angular/core';
import { ButtonModule } from 'primeng/button';
import { CardModule } from 'primeng/card';
import { InputNumberModule } from 'primeng/inputnumber';
import { KnobModule } from 'primeng/knob';
import { DividerModule } from 'primeng/divider';
import { InputOtpModule } from 'primeng/inputotp';
import { ProgressBarModule } from 'primeng/progressbar';


@NgModule({
  exports: [
    ButtonModule,
    CardModule,
    DividerModule,
    InputNumberModule,
    InputOtpModule,
    KnobModule,
    ProgressBarModule,
  ],
})
export class PrimengModule {}
