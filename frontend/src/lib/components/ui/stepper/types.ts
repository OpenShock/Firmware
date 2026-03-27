import type { Snippet } from "svelte";
import type { HTMLAttributes, HTMLButtonAttributes } from "svelte/elements";

export type StepperOrientation = "horizontal" | "vertical";

export type StepperStepState = "active" | "completed" | "inactive";

export interface StepperRootProps extends HTMLAttributes<HTMLDivElement> {
  value?: number;
  onValueChange?: (value: number) => void;
  orientation?: StepperOrientation;
  linear?: boolean;
  children?: Snippet;
}

export interface StepperNavProps extends HTMLAttributes<HTMLDivElement> {
  children?: Snippet;
}

export interface StepperItemProps extends HTMLAttributes<HTMLDivElement> {
  step: number;
  disabled?: boolean;
  completed?: boolean;
}

export interface StepperTriggerProps extends HTMLButtonAttributes {
  children?: Snippet;
}

export interface StepperIndicatorProps extends HTMLAttributes<HTMLSpanElement> {
  children?: Snippet;
}

export interface StepperSeparatorProps extends HTMLAttributes<HTMLDivElement> {
  children?: Snippet;
}

export interface StepperTitleProps extends HTMLAttributes<HTMLHeadingElement> {
  children?: Snippet;
}

export interface StepperDescriptionProps
  extends HTMLAttributes<HTMLParagraphElement> {
  children?: Snippet;
}

export interface StepperNextProps extends HTMLButtonAttributes {
  children?: Snippet;
}

export interface StepperPreviousProps extends HTMLButtonAttributes {
  children?: Snippet;
}
