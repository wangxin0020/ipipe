/*
 * linux/kernel/irq/mute.c
 *
 * Copyright (C) 2016, Philippe Gerum <rpm@xenomai.org>
 */

#include <linux/irq.h>
#include <linux/interrupt.h>

#include "internals.h"

LIST_HEAD(irqchip_muters);

static IPIPE_DEFINE_SPINLOCK(irqchip_list_lock);

void register_irqchip_muter(struct irqchip_muter *muter,
			    struct irq_chip *chip)
{
	unsigned long flags;

	muter->chip = chip;
	spin_lock_irqsave(&irqchip_list_lock, flags);
	list_add_tail(&muter->next, &irqchip_muters);
	spin_unlock_irqrestore(&irqchip_list_lock, flags);
}
EXPORT_SYMBOL_GPL(register_irqchip_muter);

void unregister_irqchip_muter(struct irqchip_muter *muter)
{
	unsigned long flags;
	
	spin_lock_irqsave(&irqchip_list_lock, flags);
	list_del(&muter->next);
	spin_unlock_irqrestore(&irqchip_list_lock, flags);
}
EXPORT_SYMBOL_GPL(unregister_irqchip_muter);

void irq_mute_switch(bool on)
{
	struct irqchip_muter *muter;

	WARN_ON_ONCE(!hard_irqs_disabled());

	spin_lock(&irqchip_list_lock);

	list_for_each_entry(muter, &irqchip_muters, next) {
		if (muter->chip->irq_mute)
			muter->chip->irq_mute(on);
	}

	spin_unlock(&irqchip_list_lock);
}
EXPORT_SYMBOL_GPL(irq_mute_switch);
