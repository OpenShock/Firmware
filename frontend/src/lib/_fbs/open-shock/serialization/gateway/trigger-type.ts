// automatically generated by the FlatBuffers compiler, do not modify

/* eslint-disable @typescript-eslint/no-unused-vars, @typescript-eslint/no-explicit-any, @typescript-eslint/no-non-null-assertion */

export enum TriggerType {
  /**
   * Restart the hub
   */
  Restart = 0,

  /**
   * Trigger the emergency stop on the hub, this does however not allow for resetting it
   */
  EmergencyStop = 1,

  /**
   * Enable the captive portal
   */
  CaptivePortalEnable = 2,

  /**
   * Disable the captive portal
   */
  CaptivePortalDisable = 3
}