<script lang="ts">
  let tableArr = [
    {
      position: 1,
      name: 'Hydrogen',
      symbol: 'H',
      weight: 1.0079
    }
  ]
  let totalWeight = 1.0079
</script>

<div class="flex flex-col items-center justify-center h-full">
	<div class="space-y-5">
		<h1 class="h1">ShockLink Setup</h1>
    <dl class="card list-dl">
      <button class="btn">
        <span class="flex-auto">
          <dt>WIFI NAME</dt>
        </span>
      </button>
    </dl>
    <!-- Add button to add new wifi -->
    <button class="btn variant-filled">Add</button>

    <!-- Configure RTM pin -->
    <div class="flex space-x-2">
      <input class="input variant-form-material" type="text" placeholder="RTM Pin" />
      <button class="btn variant-filled">Configure</button>
    </div>
	</div>
  <div class="card flex-col space-y-4 p-4 mt-10">
    <h1 class="h1">Pair Device</h1>
    <div class="flex space-x-2">
      <input class="input variant-form-material" type="text" placeholder="Pair Code" />
      <button class="btn variant-filled">Pair</button>
    </div>
  </div>
</div>
