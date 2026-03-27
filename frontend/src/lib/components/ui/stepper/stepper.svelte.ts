import { getContext, setContext } from "svelte";
import type { StepperOrientation, StepperStepState } from "./types.js";

const STEPPER_ROOT_KEY = Symbol("stepper-root");
const STEPPER_ITEM_KEY = Symbol("stepper-item");

// --- Root State ---

export interface StepperRootStateProps {
  value: { get current(): number; set current(v: number) };
  orientation: StepperOrientation;
  linear: boolean;
}

export class StepperRootState {
  value: { get current(): number; set current(v: number) };
  orientation: StepperOrientation;
  linear: boolean;
  totalSteps = $state(0);

  constructor(props: StepperRootStateProps) {
    this.value = props.value;
    this.orientation = props.orientation;
    this.linear = props.linear;
  }

  get isFirstStep(): boolean {
    return this.value.current === 1;
  }

  get isLastStep(): boolean {
    return this.value.current === this.totalSteps;
  }

  goToStep(step: number): void {
    if (step < 1 || step > this.totalSteps) return;
    this.value.current = step;
  }

  nextStep(): void {
    if (!this.isLastStep) {
      this.value.current = this.value.current + 1;
    }
  }

  prevStep(): void {
    if (!this.isFirstStep) {
      this.value.current = this.value.current - 1;
    }
  }
}

// --- Item State ---

export interface StepperItemStateProps {
  step: number;
  disabled: boolean;
  completed: boolean;
}

export class StepperItemState {
  root: StepperRootState;
  step: number;
  disabled: boolean;
  completed: boolean;

  constructor(root: StepperRootState, props: StepperItemStateProps) {
    this.root = root;
    this.step = props.step;
    this.disabled = props.disabled;
    this.completed = props.completed;
  }

  get isActive(): boolean {
    return this.root.value.current === this.step;
  }

  get state(): StepperStepState {
    if (this.isActive) return "active";
    if (this.completed || this.root.value.current > this.step)
      return "completed";
    return "inactive";
  }
}

// --- Context Helpers ---

export function setStepperRootContext(state: StepperRootState): void {
  setContext(STEPPER_ROOT_KEY, state);
}

export function getStepperRootContext(): StepperRootState {
  return getContext<StepperRootState>(STEPPER_ROOT_KEY);
}

export function setStepperItemContext(state: StepperItemState): void {
  setContext(STEPPER_ITEM_KEY, state);
}

export function getStepperItemContext(): StepperItemState {
  return getContext<StepperItemState>(STEPPER_ITEM_KEY);
}
